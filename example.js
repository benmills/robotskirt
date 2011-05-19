var markdown = require('./build/default/robotskirt');
var sys = require('sys');
var fs = require('fs');                       

//sys.puts("Using upskirt version "+markdown.upskirtVersion+" and robotskirt version "+markdown.version);

// Simple examples

//markdown.toHtml('# async markdown parsing!', function (html) {
  //sys.puts(html);
//});

//sys.puts(markdown.toHtmlSync("*slow*"));

// Open a file and parse it
fs.readFile('README.mkd', function (err, data) {
  markdown.toHtml(data, function (html) {
    sys.puts("\nREADME:\n");
    sys.puts(html);
  });
});                                           
