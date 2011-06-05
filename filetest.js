var compress = require("./compress");
var sys = require("sys");
var fs = require("fs");

// Read in our test file
var testfile = process.argv[2] || "filetest.js";
var enc = process.argv[3] || 'binary';
var data = fs.readFileSync(testfile, enc);
sys.puts("Got : " + data.length);

// Set output file
var fd = fs.openSync(testfile + ".gz", "w", 0644);
sys.puts("File opened");

// Create gzip stream
var gzip = new compress.Gzip;
gzip.init();

// Pump data to be compressed
var gzdata = gzip.deflate(data, enc);  // Do this as many times as required
sys.puts("Compressed size : " + gzdata.length);
fs.writeSync(fd, gzdata, null, "binary");

// Get the last bit
var gzlast = gzip.end();
sys.puts("Last bit : " + gzlast.length);
fs.writeSync(fd, gzlast, null, "binary");
fs.closeSync(fd);
sys.puts("File closed");

// See if we can uncompress it ok
var gunzip = new compress.Gunzip;
gunzip.init();
var testdata = fs.readFileSync(testfile + ".gz", "binary");
sys.puts("Test opened : " + testdata.length);
var inflated = gunzip.inflate(testdata, enc);
sys.puts("GZ.inflate.length: " + inflated.length);
sys.puts("GZ.end.length: " + gunzip.end().length);

if (data.length != inflated.length) {
    sys.puts('error! input/output lengths do not match');
}
