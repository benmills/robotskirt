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

#define READ_UNIT 1024
#define OUTPUT_UNIT 64

static Handle<Value> ToHtmlAsync (const Arguments&);
static int ToHtml (eio_req *);
static int ToHtml_After (eio_req *);

struct request {
  Persistent<Function> callback;
  char *in;
  char *out;
  size_t size;
};
 
static Handle<Value> ToHtmlAsync(const Arguments& args) {
  HandleScope scope;
  const char *usage = "usage: toHtml(markdown_string, callback)";
  if (args.Length() != 2) {
    return ThrowException(Exception::Error(String::New(usage)));
  }

  String::Utf8Value in(args[0]);
  Local<Function> callback = Local<Function>::Cast(args[1]);
  request *sr = (request *) malloc(sizeof(struct request));
  sr->callback = Persistent<Function>::New(callback);
  sr->in = *in;
  sr->out = NULL;
  sr->size = 0;

  eio_custom(ToHtml, EIO_PRI_DEFAULT, ToHtml_After, sr);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();
}

static int ToHtml(eio_req *req) {
  struct request *sr = (struct request *)req->data;
  struct mkd_renderer renderer;

  struct buf input_buf, *output_buf;

  memset(&input_buf, 0x0, sizeof(struct buf));
  input_buf.data = sr->in;
  input_buf.size = strlen(sr->in);

  output_buf = bufnew(128);
  bufgrow(output_buf, strlen(sr->in) * 1.2f);

  ups_xhtml_renderer(&renderer, 0);
  ups_markdown(output_buf, &input_buf, &renderer, 0xFF);
  ups_free_renderer(&renderer);

  //Handle<String> md = String::New(output_buf->data, output_buf->size);
  sr->out = output_buf->data;
  sr->size = output_buf->size;

  /* cleanup */
  return 0;
}

static int ToHtml_After(eio_req *req) {
  HandleScope scope;
  ev_unref(EV_DEFAULT_UC);
  struct request *sr = (struct request *)req->data;
  Local<Value> argv[1];

  argv[0] = String::New(sr->out, sr->size);
  TryCatch try_catch;
  sr->callback->Call(Context::GetCurrent()->Global(), 1, argv);
  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  sr->callback.Dispose();
  free(sr);
  return 0;
}

static Handle<Value> ToHtmlSync(const Arguments &args) {
  HandleScope scope;

  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New("String expected")));
  }

  String::Utf8Value in(args[0]);

  struct mkd_renderer renderer;
  struct buf input_buf, *output_buf;

  memset(&input_buf, 0x0, sizeof(struct buf));
  input_buf.data = *in;
  input_buf.size = strlen(*in);

  output_buf = bufnew(128);
  bufgrow(output_buf, strlen(*in) * 1.2f);

  ups_xhtml_renderer(&renderer, 0);
  ups_markdown(output_buf, &input_buf, &renderer, 0xFF);
  ups_free_renderer(&renderer);
  Handle<String> md = String::New(output_buf->data, output_buf->size);

  /* cleanup */
  bufrelease(output_buf);
  return scope.Close(md);
}
 
extern "C" void init (Handle<Object> target) {
    HandleScope scope;

    target->Set(String::New("version"), String::New("0.2.1"));
    NODE_SET_METHOD(target, "toHtml", ToHtmlAsync);
    NODE_SET_METHOD(target, "toHtmlSync", ToHtmlSync);
}
