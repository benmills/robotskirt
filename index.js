var rs = require("./build/Release/robotskirt");

var Robotskirt = {
  HtmlRenderer: rs.HtmlRenderer,
  markdownSync: rs.markdownSync,
  markdown: rs.markdown,

  toHtml: function (markdownText, callback) {
    this.markdown(new this.HtmlRenderer(), markdownText, callback);
  },

  toHtmlSync: function (markdownText) {
    return this.markdownSync(new this.HtmlRenderer(), markdownText);
  }
}

module.exports = Robotskirt;
