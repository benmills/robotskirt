var rs = require('./build/default/robotskirt');
var sys = require('sys');

rs.toHtml('# ben mills', function (data) {
  sys.puts(data);
});

sys.puts("Waiting");
