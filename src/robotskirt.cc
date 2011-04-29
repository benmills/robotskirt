// addons/functions/v0.1/func.cc
#include <v8.h>
#include <node.h>

extern "C" {
  #include"markdown.h"
  #include"xhtml.h"
}

#define READ_UNIT 1024
#define OUTPUT_UNIT 64

using namespace v8;  
 
extern "C" char *markdown(char *text)
{
  struct buf  *ib, *ob;
  struct mkd_renderer renderer;
  size_t i, iterations = 1;

  /* performing markdown parsing */
  ob = bufnew(OUTPUT_UNIT);
  ib = bufnew(READ_UNIT);
  bufputs(ib, text);

  for (i = 0; i < iterations; ++i) {
    ob->size = 0;

    ups_xhtml_renderer(&renderer, 0);
    ups_markdown(ob, ib, &renderer, 0xFF);
    ups_free_renderer(&renderer);
  }

  /* writing the result to stdout */
  char *output = ob->data;
  /*fwrite(ob->data, 1, ob->size, stdout);*/

  /* cleanup */
  bufrelease(ib);
  bufrelease(ob);

  return output;
}

static Handle<Value> ToHtml(const Arguments& args) {
  HandleScope scope;
 
  if (args.Length() < 1) {
    return ThrowException(Exception::TypeError(String::New("Bad argument")));
  }

  char *t;
  String::Utf8Value in(args[0]);
  t = markdown((char*)*in);

  Handle<String> md = String::New(t);
  return scope.Close(md);
}
 
extern "C" void
init (Handle<Object> target)
{
    HandleScope scope;
    target->Set(String::New("version"), String::New("0.1"));
    NODE_SET_METHOD(target, "toHtml", ToHtml);
}
