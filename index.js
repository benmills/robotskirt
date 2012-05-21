var rs = require("./build/Release/robotskirt");

var Robotskirt = {
  HtmlRenderer: rs.HtmlRenderer,
  markdownSync: rs.markdownSync,
  markdown: rs.markdown,
  flags: {  EXT_AUTOLINK: rs.EXT_AUTOLINK,
            EXT_FENCED_CODE: rs.EXT_FENCED_CODE,
            EXT_LAX_HTML_BLOCKS: rs.EXT_LAX_HTML_BLOCKS,
            EXT_NO_INTRA_EMPHASIS: rs.EXT_NO_INTRA_EMPHASIS,
            EXT_SPACE_HEADERS: rs.EXT_SPACE_HEADERS,
            EXT_STRIKETHROUGH: rs.EXT_STRIKETHROUGH,
            EXT_TABLES: rs.EXT_TABLES },

  toHtml: function (markdownText, callback, flags) {
    this.markdown(new this.HtmlRenderer(), markdownText, callback, flags);
  },

  toHtmlSync: function (markdownText, flags) {
    return this.markdownSync(new this.HtmlRenderer(), markdownText, flags);
  }
}

module.exports = Robotskirt;
