#include <node.h>
#include <node_events.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>

#include <stdio.h>
#include <node_buffer.h>
#include "buffer_compat.h"

#define CHUNK 16384

#define THROW_IF_NOT(condition, text) if (!(condition)) { \
      return ThrowException(Exception::Error (String::New(text))); \
    }
#define THROW_IF_NOT_A(condition, bufname, x) if (!(condition)) { \
   char bufname[128] = {0}; \
   sprintf x; \
   return ThrowException(Exception::Error (String::New(bufname))); \
   }

using namespace v8;
using namespace node;

static Persistent<String> use_buffers;

class Gzip : public EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", GzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "deflate", GzipDeflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", GzipEnd);

    target->Set(String::NewSymbol("Gzip"), t->GetFunction());
  }

  int GzipInit(int level) {
    int ret;
    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    // +16 to windowBits to write a simple gzip header and trailer around the
    // compressed data instead of a zlib wrapper
    ret = deflateInit2(&strm, level, Z_DEFLATED, 16+MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    return ret;
  }

  int GzipDeflate(char* data, int data_len, char** out, int* out_len) {
    int ret = 0;
    char* temp;
    int i=1;

    *out = NULL;
    *out_len = 0;
    ret = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (Bytef*)data;
      do {
        temp = (char *)realloc(*out, CHUNK*i +1);
        if (temp == NULL) {
          return Z_MEM_ERROR;
        }
        *out = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*)*out + *out_len;
        ret = deflate(&strm, Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        *out_len += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);

      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  int GzipEnd(char** out, int* out_len) {
    int ret;
    char* temp;
    int i = 1;

    *out = NULL;
    *out_len = 0;
    strm.avail_in = 0;
    strm.next_in = NULL;

    do {
      temp = (char *)realloc(*out, CHUNK*i);
      if (temp == NULL) {
        return Z_MEM_ERROR;
      }
      *out = temp;
      strm.avail_out = CHUNK;
      strm.next_out = (Bytef*)*out + *out_len;
      ret = deflate(&strm, Z_FINISH);
      assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
      *out_len += (CHUNK - strm.avail_out);
      i++;
    } while (strm.avail_out == 0);

    deflateEnd(&strm);
    return ret;
  }

 protected:

  static Handle<Value> New(const Arguments& args) {
    HandleScope scope;

    Gzip *gzip = new Gzip();
    gzip->Wrap(args.This());

    return args.This();
  }

  /* options: encoding: string [null] if set output strings, else buffers
   *          level:    int    [-1]   (compression level)
   */
  static Handle<Value> GzipInit(const Arguments& args) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    HandleScope scope;

    int level = Z_DEFAULT_COMPRESSION;
    gzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(String::NewSymbol("encoding"));
      Local<Value> lev = options->Get(String::NewSymbol("level"));

      if (enc != Undefined() && enc != Null()) {
        gzip->encoding = ParseEncoding(enc);
        gzip->use_buffers = false;
      }
      if (lev != Undefined() && lev != Null()) {
        level = lev->ToInt32()->Value();
      }
    }

    int r = gzip->GzipInit(level);
    return scope.Close(Integer::New(r));
  }

  static Handle<Value> GzipDeflate(const Arguments& args) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    HandleScope scope;

    char* buf;
    ssize_t len;
    // deflate a buffer or a string?
    if (Buffer::HasInstance(args[0])) {
       // buffer
       Local<Object> buffer = args[0]->ToObject();
       len = BufferLength(buffer);
       buf = BufferData(buffer);
    } else {
      // string, default encoding is utf8
      enum encoding enc = args.Length() == 1 ? UTF8 : ParseEncoding(args[1], UTF8);
      len = DecodeBytes(args[0], enc);
      THROW_IF_NOT_A (len >= 0, xmsg, (xmsg, "Bad DecodeBytes result: %zd", len));

      // TODO memory leak?
      buf = new char[len];
      ssize_t written = DecodeWrite(buf, len, args[0], enc);
      assert(written == len);
    }

    char* out;
    int out_size;
    int r = gzip->GzipDeflate(buf, len, &out, &out_size);
    THROW_IF_NOT_A (r >= 0, xmsg, (xmsg, "gzip deflate: error(%d) %s", r, gzip->strm.msg));
    THROW_IF_NOT_A (out_size >= 0, xmsg, (xmsg, "gzip deflate: negative output size: %d", out_size));

    if (gzip->use_buffers) {
      // output compressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
        free(out);
      }
      return scope.Close(Local<Value>::New(b->handle_));
    } else if (out_size == 0) {
      return scope.Close(String::Empty());
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(out, out_size, gzip->encoding);
      free(out);
      return scope.Close(outString);
    }
  }

  static Handle<Value> GzipEnd(const Arguments& args) {
    Gzip *gzip = ObjectWrap::Unwrap<Gzip>(args.This());

    HandleScope scope;

    char* out;
    int out_size;

    int r = gzip->GzipEnd(&out, &out_size);
    THROW_IF_NOT_A (r >=0, xmsg, (xmsg, "gzip end: error(%d) %s", r, gzip->strm.msg));
    THROW_IF_NOT_A (out_size >= 0, xmsg, (xmsg, "gzip end: negative output size: %d", out_size));

    if (gzip->use_buffers) {
      // output compressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
        free(out);
      }
      return scope.Close(Local<Value>::New(b->handle_));
    } else if (out_size == 0) {
      return scope.Close(String::Empty());
    } else {
      // output compressed data in a binary string
      Local<Value> outString = Encode(out, out_size, gzip->encoding);
      free(out);
      return scope.Close(outString);
    }
  }

  Gzip() : EventEmitter(), use_buffers(true), encoding(BINARY) {
  }

  ~Gzip() {
  }

 private:

  z_stream strm;
  bool use_buffers;
  enum encoding encoding;
};


class Gunzip : public EventEmitter {
 public:
  static void Initialize(v8::Handle<v8::Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "init", GunzipInit);
    NODE_SET_PROTOTYPE_METHOD(t, "inflate", GunzipInflate);
    NODE_SET_PROTOTYPE_METHOD(t, "end", GunzipEnd);

    target->Set(String::NewSymbol("Gunzip"), t->GetFunction());
  }

  int GunzipInit() {
    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    // +16 to decode only the gzip format (no auto-header detection)
    int ret = inflateInit2(&strm, 16+MAX_WBITS);
    return ret;
  }

  int GunzipInflate(const char* data, int data_len, char** out, int* out_len) {
    int ret = 0;
    char* temp;
    int i=1;

    *out = NULL;
    *out_len = 0;

    while (data_len > 0) {
      if (data_len > CHUNK) {
        strm.avail_in = CHUNK;
      } else {
        strm.avail_in = data_len;
      }

      strm.next_in = (Bytef*)data;

      do {
        temp = (char *)realloc(*out, CHUNK*i);
        if (temp == NULL) {
          return Z_MEM_ERROR;
        }
        *out = temp;
        strm.avail_out = CHUNK;
        strm.next_out = (Bytef*)*out + *out_len;
        ret = inflate(&strm, Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        switch (ret) {
        case Z_NEED_DICT:
          ret = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
          (void)inflateEnd(&strm);
          return ret;
        }
        *out_len += (CHUNK - strm.avail_out);
        i++;
      } while (strm.avail_out == 0);
      data += CHUNK;
      data_len -= CHUNK;
    }
    return ret;
  }

  void GunzipEnd() {
    inflateEnd(&strm);
  }

 protected:

  static Handle<Value> New(const Arguments& args) {
    HandleScope scope;

    Gunzip *gunzip = new Gunzip();
    gunzip->Wrap(args.This());

    return args.This();
  }

  /* options: encoding: string [null], if set output strings, else buffers
   */
  static Handle<Value> GunzipInit(const Arguments& args) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    HandleScope scope;

    gunzip->use_buffers = true;
    if (args.Length() > 0) {
      THROW_IF_NOT (args[0]->IsObject(), "init argument must be an object");
      Local<Object> options = args[0]->ToObject();
      Local<Value> enc = options->Get(String::NewSymbol("encoding"));

      if (enc != Undefined() && enc != Null()) {
        gunzip->encoding = ParseEncoding(enc);
        gunzip->use_buffers = false;
      }
    }

    int r = gunzip->GunzipInit();
    return scope.Close(Integer::New(r));
  }

  static Handle<Value> GunzipInflate(const Arguments& args) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    HandleScope scope;

    char* buf;
    ssize_t len;
    // inflate a buffer or a binary string?
    if (Buffer::HasInstance(args[0])) {
       // buffer
       Local<Object> buffer = args[0]->ToObject();
       len = BufferLength(buffer);
       buf = BufferData(buffer);
    } else {
      // string, default encoding is binary. this is much worse than using a buffer
      fprintf(stdout, "  args.length %d\n", args.Length());
      enum encoding enc = args.Length() == 1 ? BINARY : ParseEncoding(args[1], BINARY);
      len = DecodeBytes(args[0], enc);
      fprintf(stdout, "  decodeBytes %zd\n", len);
      THROW_IF_NOT_A (len >= 0, xmsg, (xmsg, "Bad DecodeBytes result: %zd", len));

      // TODO memory leak?
      buf = new char[len];
      ssize_t written = DecodeWrite(buf, len, args[0], enc);
      assert(written == len);
    }

    char* out;
    int out_size;
    int r = gunzip->GunzipInflate(buf, len, &out, &out_size);
    THROW_IF_NOT_A (r >= 0, xmsg, (xmsg, "gunzip inflate: error(%d) %s", r, gunzip->strm.msg));
    THROW_IF_NOT_A (out_size >= 0, xmsg, (xmsg, "gunzip inflate: negative output size: %d", out_size));

    if (gunzip->use_buffers) {
      // output decompressed data in a buffer
      Buffer* b = Buffer::New(out_size);
      if (out_size != 0) {
        memcpy(BufferData(b), out, out_size);
        free(out);
      }
      return scope.Close(Local<Value>::New(b->handle_));
    } else if (out_size == 0) {
      return scope.Close(String::Empty());
    } else {
      // output decompressed data in an encoded string
      Local<Value> outString = Encode(out, out_size, gunzip->encoding);
      free(out);
      return scope.Close(outString);
    }
  }

  static Handle<Value> GunzipEnd(const Arguments& args) {
    Gunzip *gunzip = ObjectWrap::Unwrap<Gunzip>(args.This());

    HandleScope scope;
    gunzip->GunzipEnd();
    return scope.Close(Undefined());
  }

  Gunzip() : EventEmitter(), use_buffers(true), encoding(BINARY) {
  }

  ~Gunzip() {
  }

 private:

  z_stream strm;
  bool use_buffers;
  enum encoding encoding;
};

extern "C" void init(Handle<Object> target) {
  HandleScope scope;
  Gzip::Initialize(target);
  Gunzip::Initialize(target);
}
