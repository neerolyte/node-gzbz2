import Options
import platform
from os import unlink, symlink, popen
from os.path import exists 

srcdir = "."
blddir = "build"
VERSION = "0.1.0"
OSTYPE = platform.system()

def set_options(opt):
  opt.tool_options("compiler_cxx")
  opt.tool_options("compiler_cc")

def configure(conf):
  conf.check_tool("compiler_cxx")
  conf.check_tool("compiler_cc")
  conf.check_tool("node_addon")

  conf.check(lib='z', libpath=['/usr/lib', '/usr/local/lib', '/opt/local/lib'], uselib_store='ZLIB')

def build(bld):
  obj = bld.new_task_gen("cxx", "shlib", "node_addon")
  obj.cxxflags = ['-D_FILE_OFFSET_BITS=64', '-D_LARGEFILE_SOURCE', '-Wall', '-O2']
  obj.libpath = ['/usr/lib', '/usr/local/lib']
  obj.includes = ['/usr/include', '/usr/local/include']
  obj.ldflags = []

  if OSTYPE == 'Darwin':
    obj.cxxflags.append('-mmacosx-version-min=10.4')
    obj.ldflags.append('-mmacosx-version-min=10.4')
    obj.libpath.append('/opt/local/lib')
    obj.includes.append('/opt/local/include')
  else:
    # default build flags, add special cases if needed
    pass

  obj.target = "compress"
  obj.source = "compress.cc"
  obj.uselib = "ZLIB"

def shutdown():
  # HACK to get compress.node out of build directory.
  # better way to do this?
  if Options.commands['clean']:
    if exists('compress.node'): unlink('compress.node')
  else:
    if exists('build/default/compress.node') and not exists('compress.node'):
      symlink('build/default/compress.node', 'compress.node')
