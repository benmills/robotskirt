# Robotskirt

Robotskirt is a [Node.JS](http://nodejs.org) wrapper for the [Sundown](https://github.com/vmg/sundown)
library.

It was inspired by the Redcarpet gem [released by GitHub](https://github.com/blog/832-rolling-out-the-redcarpet) (the bindings to [Ruby](http://www.ruby-lang.org)).  
With the arrival of version 2 after much work, Robotskirt now mirrors every feature of Redcarpet, see below.  
It even has additional features!

Full documentation can be found under the `doc` folder.  
Robotskirt is distributed under the **MIT license**, see `LICENSE`.

## Performance

Thanks to Sundown, Robotskirt is able to render markdown many times faster than other Markdown libraries.  
With v2, efforts have been put to make it even lighter.

Robotskirt includes a small script to benchmark it against other popular markdown libraries.  
It runs the official Markdown test suite 1000 times with each item.

Results on a Thinkpad T400 running Ubuntu 12.04 and
Node 0.8.8 (currently the latest stable version):

```bash
$ node benchmark --bench
[1] robotskirt (reuse all) completed in 1363ms.
[2] robotskirt (convenience, reuse all) completed in 1359ms.
[3] robotskirt (new renderer and parser) completed in 3853ms.
[4] robotskirt (convenience, new parser) completed in 1545ms.
[5] marked completed in 3874ms.
[6] discount completed in 6122ms.
6 targets benchmarked successfully.
```

## Install

The best way to install Robotskirt is by using [NPM](https://github.com/isaacs/npm).  
If you want to install it globally, remember to use `sudo` and `-g`.

```bash
npm install robotskirt
```

**Important:** you *don't need* to have Sundown installed: Robotskirt comes bundled  
with a specific Sundown version. Just install Robotskirt as any other module.

Robotskirt uses `node-waf` to compile
(although we'll switch to [Node-GYP](https://github.com/TooTallNate/node-gyp) soon).

## Getting started

Currently there are two ways of using Robotskirt:
[normal](#the-normal-way) and [convenience](#the-convenience-way).  
We recommend you to learn both (hey, it's just two classes!) and see the [examples](#examples).

## The Normal Way

To parse Markdown, we first need a **renderer**. It takes the parsed Markdown,  
and produces the final output (can be HTML, XHTML,
[ANSI](https://github.com/benmills/robotskirt/blob/master/examples/ansi-rend.js), plain text, ...).

On most cases you will use Sundown's (X)HTML renderer:

```javascript
var rs = require('robotskirt');
var renderer = new rs.HtmlRenderer();
```

Then, you make a **parser** that uses your renderer:

```javascript
var parser = new rs.Markdown(renderer);
```

That's it! You can now start rendering your markdown:

```javascript
parser.render('Hey, *this* is `code` with ÚŦF châracters!')
// '<p>Hey, <em>this</em> is <code>code</code> with ÚŦF châracters!</p>\n'
```

**Always reuse yor parsers/renderers!** As you can see in the [benchmark](#performance),  
making and using the same pair to render everything saves a _lot_ of time.  
If you can't reuse them (for example, because the flags are supplied by the user),  
consider using [the convenience way](#the-convenience-way).

OK. Want to customize the output a bit? Keep reading.

### Using markdown extensions

Just using `new Markdown(renderer)` will parse **pure markdown**.
However, you can have it  
understand special _extensions_ such as fenced code blocks,
strikethrough, tables and more!

For example, the following will enable tables and autolinking:

```javascript
var parser = new rs.Markdown(renderer, [rs.EXT_TABLES, rs.EXT_AUTOLINK]);
```

You can see the full list of extensions in the docs.

### HTML rendering flags

Just as with extensions, you can pass certain flags to the HTML renderer.

For example, the following will use strict XHTML
and skip all the `<image>` tags:

```javascript
var renderer = new rs.HtmlRenderer([rs.HTML_USE_XHTML, rs.HTML_SKIP_IMAGES]);
```

You can see the full list of HTML flags in the docs.

### UTF handling

Sundown is fully UTF-8 aware, both when handling and rendering.  
Robotskirt will take care of the encoding and decoding tasks for you.

### Custom renderers!

A renderer is just a set of functions.  
Each time the parser finds a piece of Markdown it'll call the appropiate function in the renderer.  
If the function is not set (`undefined`), the Markdown will be skipped or copied untouched.

Some use cases of custom renderers:

#### Highlighting code blocks

```javascript
var renderer = new rs.HtmlRenderer();
renderer.blockcode = function (code, language) {
  if (language === undefined) {
    //No language was provided, don't highlight
    return '<pre>' + escapeHtml(code) + '</pre>';
  }
  return pygments.highlight(code, {"lang": language, "indent": 2});
};
```

You can see the full list of renderer functions in the docs.

#### Renderer from scratch

If you don't feel comfortable extending the `HtmlRenderer` class,  
you can build a renderer from scratch by extending the base class: `Renderer`.  
All renderers inherit from this class. It contains all functions set to `undefined`.

## The Convenience Way

When you don't need custom renderers at all, you can just write:

```javascript
var rs = require('robotskirt');
var parser = rs.Markdown.std();
parser.render(...);
```

That'll build a renderer/parser pair for you.  
It's **faster than building them manually**, because it happens natively.

You can pass **extension** and **HTML** flags to it, respectively:

```javascript
var parser = rs.Markdown.std([rs.EXT_TABLES, rs.EXT_AUTOLINK],
                             [rs.HTML_USE_XHTML, rs.HTML_SKIP_IMAGES]);
parser.render('This becomes http://autolink.ed in XHTML!');
// '<p>This becomes <a href="http://autolink.ed">http://autolink.ed</a> in XHTML!</p>\n'
```

Keep in mind that **no other types of renderer can be chosen**,  
and **you don't have access to the HTML renderer used**.

## Examples

TODO