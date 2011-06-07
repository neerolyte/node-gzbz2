NAME
----

node-compress - A Node.js interface to streaming gzip compression.

INSTALL
-------

To install, ensure that you have libz installed, and run:

npm install node-compress

or

node-waf configure
node-waf build

This will put the compress.node binary module in build/default.

Note: on osx I configure may report not finding libz if when build succeeds, still working on this issue.

Quick Gzip example
------------------

    var compress = require("./compress");
    var sys = require("sys");

    // Create gzip stream
    var gzip = new compress.Gzip;
    // binary string output
    gzip.init({encoding: 'binary'});

    // Pump data to be compressed
    var gzdata1 = gzip.deflate("My data that needs ", "ascii");
    sys.puts("Compressed size : "+gzdata1.length);

    // treat string as binary encoded
    var gzdata2 = gzip.deflate("to be compressed. 01234567890.");
    sys.puts("Compressed size : "+gzdata2.length);

    var gzdata3 = gzip.end(); // important to capture end data
    sys.puts("Last bit : "+gzdata3.length);

    // Normally stream this out as its generated, but just print here
    var gzdata = gzdata1+gzdata2+gzdata3;
    sys.puts("Total compressed size : "+gzdata.length);

Quick Gunzip example
--------------------

    var compress = require("./compress");
    var sys = require("sys");
    var fs = require("fs");

    var gunzip = new compress.Gunzip;
    gunzip.init({encoding: "utf8"});

    var gzdata = fs.readFileSync("somefile.gz", "binary");
    var inflated = gunzip.inflate(testdata, "binary");
    gunzip.end(); // returns nothing

Versions
--------

* 0.1.*:
    * buffer capable (on by default, this changes the default api use)
        * buffer input inflate/deflate always allowed, buffer output from same is the default
    * init accepts options object to configure buffer behavior, compression, and inflated encoding etc:
        * Gzip.init({encoding: enc, level: [0-9]}), level controls compression level 0 = none, 1 = lowest etc.
        * Gunzip.init({encoding: enc})
        * if an encoding is provided to init(), then the output of inflate/deflate will be a string
        * when providing encodings (either for input our output) for binary data, 'binary' is the only viable encoding, as base64 is not currenlty supported
    * inflate accepts a buffer or binary string[+encoding[default = 'binary']], output will be a buffer or a string encoded according to init options
    * deflate accepts a buffer or string[+encoding[default = 'utf8']], output will be a buffer or a string encoded according to init options

* 0.0.*:
    * all string based, encodings in inflate/deflate methods, no init params
