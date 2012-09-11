#!/usr/bin/env node

/**
 * A beautiful ANSI renderer for Robotskirt.
 * To use (standalone):
 *
 *     ./ansi-rend.js < myfile.md | less -R
 *
 * Depends on Color.JS.
 * You'll also need Pipette to run it as a standalone script.
 *
 * Syntax highlighting and text justification coming soon!
 **/

var rs = require('robotskirt')
  , colors = require('colors')
  , util = require('util');

function AnsiRenderer(opts) {
  if (opts===undefined) opts = {};
  
  var rend = new rs.Renderer();
  
  function paintLink(url) {//TODO:check for not punctuation
    if (url.length>32) {
      url = url.substr(0,32)
      while (url.charAt(url.length-1)=='.')
        url = url.substr(0,url.length-1);
    }
    return url.underline;
  }
  
  function indent(text, pref, firstpref) {
    if (firstpref===undefined) firstpref = pref;
    text= firstpref+text.replace(/\n\r?/g, "\n"+pref);
    while (text.length>0) {
      char = text.charAt(text.length-1)
      if (char == '\n' || (!char.match(/\s/))) break;
      text=text.substr(0,text.length-1);
    }
    return text;
  }
  
  rend.normal_text = function (text) {
    return text.replace(/\s+/g, ' ')
  };
  
  rend.header = function (text, level) {
    pref = ''
    for (i=0; i<level; i++) pref += '#'
    
    return util.format('%s %s'.bold+'\n\n', pref, text)
  };
  rend.paragraph = function (text) {
    //TODO: wrap
    if (text.indexOf('\n') != -1 || text.length < 240) return text.trim()+'\n\n'
    return '  '+text+'\n\n'
  };
  rend.blockcode = function (code, lang) {
    //TODO: highlight, lang
    if (code.charAt(code.length-1) != '\n') code += '\n'
    return indent(code,'    ').white+"\n";
  }
  //rend.blockquote = 
  
  
  rend.emphasis = colors.italic
  rend.double_emphasis = colors.bold
  rend.triple_emphasis = function (text) {
    return text.bold.italic
  };
  
  rend.link = function (link, title, content) {
    return content.cyan.underline;
  };
  
  rend.codespan = function (text) {
    return util.format('`%s`'.white, text);
  };
  
  rend.autolink = function (link) {
    return paintLink(link).cyan;
  };
  rend.linebreak = function () {return '\n'}
  
  rend.image = function (link, title, alt) {
    //TODO: box
    return util.format('[ %s ]'.yellow, alt)
  }
  
  rend.list = function (text) {return text+'\n'}
  rend.listitem = function (text) {
    if (text.charAt(text.length-1)!='\n') text+='\n';
    return indent(text.trimLeft(), "   ", " • ");
  }
  
  return rend;
}


//FIXME: universal line breaks
(new (require('pipette').Sink)(process.stdin)).on('data', function (input) {
  var rend = AnsiRenderer()
  var md = new rs.Markdown(rend, ~0)
  
  process.stdout.write(md.render(input));
});
process.stdin.resume();
