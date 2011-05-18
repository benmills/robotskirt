#include <v8.h>
#include <node.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

using namespace std;
using namespace node;
using namespace v8;

extern "C" {
  #include"markdown.h"
  #include"xhtml.h"
}

static Handle<Value> ToHtmlAsync (const Arguments&);
static int ToHtml (eio_req *);
static int ToHtml_After (eio_req *);

#define READ_UNIT 1024
#define OUTPUT_UNIT 64
 
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

  /* cleanup */
  bufrelease(ib);
  bufrelease(ob);

  return output;
}

struct request {
  char *in;
  char *out;
  Persistent<Function> cb;
};

static Handle<Value> ToHtmlAsync(const Arguments& args) {
  HandleScope scope;
  const char *usage = "usage: toHtml(markdown_string, callback)";
  if (args.Length() != 2) {
    return ThrowException(Exception::Error(String::New(usage)));
  }

  String::Utf8Value in(args[0]);
  Local<Function> cb = Local<Function>::Cast(args[1]);

  request *sr = (request *)
  malloc(sizeof(struct request) + in.length() + 1);

  sr->cb = Persistent<Function>::New(cb);
  sr->in = (char*)*in;

  eio_custom(ToHtml, EIO_PRI_DEFAULT, ToHtml_After, sr);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();
}

static int ToHtml_After(eio_req *req) {
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);
  struct request * sr = (struct request *)req->data;
  Local<Value> argv[1];

  argv[0] = String::New(sr->out);
  TryCatch try_catch;
  sr->cb->Call(Context::GetCurrent()->Global(), 1, argv);
  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }
  sr->cb.Dispose();
  free(sr);
  return 0;
}

static int ToHtml(eio_req *req) {
  struct request *sr = (struct request *)req->data;
  char *t = markdown(sr->in);
  sr->out = t;
  return 0;
}

tatic Handle<Value> ToHtmlSync(const Arguments& args) {
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
    target->Set(String::New("version"), String::New("0.2"));
    NODE_SET_METHOD(target, "toHtml", ToHtmlAsync);
    NODE_SET_METHOD(target, "toHtmlSync", ToHtmlSync);
}
