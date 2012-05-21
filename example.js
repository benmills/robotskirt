var rs = require('./index.js');
var fs = require('fs');

// Simple examples
rs.toHtml("# async", function (html) {
  process.stdout.write(html);
});

process.stdout.write(rs.toHtmlSync("# sync markdown parsing.."));

//Open a file and parse it
fs.readFile('README.mkd', function (err, data) {
  rs.toHtml(data.toString(), function (html) {
    process.stdout.write(html);
  });
});

// Reuse a renderer
var renderer = new rs.HtmlRenderer();

rs.markdown(renderer, "*this is bold* http://www.benmills.org", function (html) {
  console.log(html.toString());
});

// Use a flag

rs.markdown(renderer, "*this is bold* http://www.benmills.org", function (html) {
  console.log(html.toString());
}, [rs.flags.EXT_AUTOLINK]);

rs.toHtml("*this is bold* http://www.benmills.org", function (html) {
  console.log(html.toString());
}, [rs.flags.EXT_AUTOLINK]);

output = rs.toHtmlSync("*this is bold* http://www.benmills.org", [rs.flags.EXT_AUTOLINK]);
console.log(output.toString());
