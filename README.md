# Robotskirt

Robotskirt is a [Node.JS](http://nodejs.org) wrapper for the [Sundown](https://github.com/vmg/sundown)
library.

It was inspired by the Redcarpet gem [released by GitHub](https://github.com/blog/832-rolling-out-the-redcarpet) (the bindings to [Ruby](http://www.ruby-lang.org)).  
With the arrival of version 2 after much work, Robotskirt now mirrors every feature of Redcarpet, see below.  
It even has [additional features](#other-utilities)!

Full documentation can be found under the `doc` folder.  
Robotskirt is distributed under the **MIT license**, see `LICENSE`.

## Performance

Thanks to Sundown, Robotskirt is able to render markdown many times faster than other Markdown libraries.  
With v2, efforts have been put to make it even lighter.

Sundown is well known for its **security**, **speed** and **flexibility**.  
Robotskirt benefits from these features and tries to make the wrapping layer as thin as possible.

Robotskirt includes a small script to benchmark it against other popular markdown libraries.  
It runs the official Markdown test suite 1000 times with each item.

Results on a Thinkpad T400 running Ubuntu 12.04 and
Node 0.8.8 (currently the latest stable version):

```bash
$ node benchmark --bench
[1] robotskirt (reuse all) completed in 1354ms.
[2] robotskirt (convenience, reuse all) completed in 1353ms.
[3] robotskirt (new renderer and parser) completed in 3816ms.
[4] robotskirt (convenience, new parser) completed in 1534ms.
[5] marked completed in 3842ms.
[6] discount completed in 6025ms.
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

Starting with `v2.7`, Robotskirt uses the preferred [Node-GYP](https://github.com/TooTallNate/node-gyp) to compile.

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

## Other utilities

Robotskirt includes some useful utilities. Code speaks by itself:

##### [Houdini](https://github.com/vmg/houdini), the escapist

``` javascript
> var rs = require('robotskirt')
> rs.houdini.escapeHTML('<b>Some code to escape.</b> <a title="Click me!">Me & you.</a>')
'&lt;b&gt;Some code to escape.&lt;&#47;b&gt; &lt;a title=&quot;Click me!&quot;&gt;Me &amp; you.&lt;&#47;a&gt;'
> rs.houdini.unescapeURL('Include+5%25+me%2Bin+a-query%3F+W%C3%ADth%C3%99TF%21')
'Include 5% me in a-query? WíthÙTF!'
```

##### [SmartyPants](http://daringfireball.net/projects/smartypants)

Often used in conjunction with Markdown.  
It makes "smart" punctuation. See more on [its homepage](http://daringfireball.net/projects/smartypants).

```javascript
> rs.smartypantsHtml('And I said ---to him--- "no worries"...');
'And I said &mdash;to him&mdash; &ldquo;no worries&rdquo;&hellip;'
```

Sundown implements SmartyPants with the same
speed and security as usual.

##### Sundown's Autolink-er

```javascript
//COMING SOON!
```

##### Version stuff

``` javascript
> rs.versions.sundown
<Version 1.16.0>
> rs.versions.robotskirt.toString() //String formatted version
'2.5.1'
> console.log('Sundown is at %s. Robotskirt is at %s',
... rs.versions.sundown, rs.versions.robotskirt);
Sundown is at 1.16.0. Robotskirt is at 2.5.1.
> rs.versions.sundown.minor
16
> rs.versions.robotskirt instanceof rs.Version
true
```

