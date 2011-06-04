var markdown = require('./build/default/robotskirt');
var sys = require('sys');
var fs = require('fs');                       

// Simple examples

markdown.toHtml("# async", function (html) {
  sys.puts(html);
});

sys.puts(markdown.toHtmlSync("# sync markdown parsing.."));

//Open a file and parse it
fs.readFile('README.mkd', function (err, data) {
  markdown.toHtml(data.toString(), function (html) {
    sys.puts(html);
  });
});                                           
