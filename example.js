var markdown = require('./build/Release/robotskirt');
var fs = require('fs');                       

// Simple examples

markdown.toHtml("# async", function (html) {
  process.stdout.write(html);
});

process.stdout.write(markdown.toHtmlSync("# sync markdown parsing.."));

//Open a file and parse it
fs.readFile('README.mkd', function (err, data) {
  markdown.toHtml(data.toString(), function (html) {
    process.stdout.write(html);
  });
});                                           
