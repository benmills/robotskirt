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
 
char *markdown(char *text)
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
  Persistent<Function> cb;
  char *out;
  char *in;
};

static Handle<Value> ToHtmlAsync(const Arguments& args) {
  HandleScope scope;
  const char *usage = "usage: toHtml(markdown_string, callback)";
  if (args.Length() != 2) {
    return ThrowException(Exception::Error(String::New(usage)));
  }

  String::Utf8Value in(args[0]);
  Local<Function> cb = Local<Function>::Cast(args[1]);
  request *sr = (request *) malloc(sizeof(struct request));
  sr->cb = Persistent<Function>::New(cb);
  sr->in = (char *) malloc(in.length() + 1);
  strncpy(sr->in, *in, in.length() + 1);
  sr->out = NULL;

  eio_custom(ToHtml, EIO_PRI_DEFAULT, ToHtml_After, sr);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();
}

static int ToHtml(eio_req *req) {
  struct request *sr = (struct request *)req->data;
  sr->out = markdown(sr->in);
  return 0;
}

static int ToHtml_After(eio_req *req) {
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);
  struct request *sr = (struct request *)req->data;
  Local<Value> argv[1];

  argv[0] = String::New(sr->out);
  TryCatch try_catch;
  sr->cb->Call(Context::GetCurrent()->Global(), 1, argv);
  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }
  sr->cb.Dispose();
  free(sr->in);
  free(sr);
  return 0;
}

static Handle<Value> ToHtmlSync(const Arguments &args) {
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
 
extern "C" void init (Handle<Object> target) {
    HandleScope scope;

    int ver_major;
    int ver_minor;
    int ver_revision;
    ups_version(&ver_major, &ver_minor, &ver_revision);
    char upsver[10];
    sprintf(upsver, "%c.%c.%c", ver_major, ver_minor, ver_revision);

    target->Set(String::New("version"), String::New("0.2.0"));
    target->Set(String::New("upskirtVersion"), String::New(upsver));
    NODE_SET_METHOD(target, "toHtml", ToHtmlAsync);
    NODE_SET_METHOD(target, "toHtmlSync", ToHtmlSync);
}
