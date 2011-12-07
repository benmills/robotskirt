#include <v8.h>
#include <node.h>
#include <node_buffer.h>
#include <string>
#include <stdlib.h>
#include <unistd.h>

using namespace std;
using namespace node;
using namespace v8;

extern "C" {
  #include"markdown.h"
  #include"html.h"
}

#define OUTPUT_UNIT 64

// Credit: @samcday
// http://sambro.is-super-awesome.com/2011/03/03/creating-a-proper-buffer-in-a-node-c-addon/ 
#define MAKE_FAST_BUFFER(NG_SLOW_BUFFER, NG_FAST_BUFFER)      \
  Local<Function> NG_JS_BUFFER = Local<Function>::Cast(       \
    Context::GetCurrent()->Global()->Get(                     \
      String::New("Buffer")));                                \
                                                              \
  Handle<Value> NG_JS_ARGS[3] = {                             \
    NG_SLOW_BUFFER->handle_,                                  \
    Integer::New(Buffer::Length(NG_SLOW_BUFFER)),             \
    Integer::New(0)                                           \
  };                                                          \
                                                              \
  NG_FAST_BUFFER = NG_JS_BUFFER->NewInstance(3, NG_JS_ARGS);

static Handle<Value> ToHtmlAsync (const Arguments&);
static void ToHtml (eio_req *);
static int ToHtml_After (eio_req *);

struct request {
  Persistent<Function> callback;
  string in;
  string out;
  int out_size;
};
 
static Handle<Value> ToHtmlAsync(const Arguments& args) {
  HandleScope scope;

  const char *usage = "usage: toHtml(markdown_string, callback)";
  if (args.Length() != 2) {
    return ThrowException(Exception::Error(String::New(usage)));
  }

  String::Utf8Value in(args[0]);
  Local<Function> callback = Local<Function>::Cast(args[1]);

  request *sr = new request;
  sr->callback = Persistent<Function>::New(callback);
  sr->in = *in;

  eio_custom(ToHtml, EIO_PRI_DEFAULT, ToHtml_After, sr);
  ev_ref(EV_DEFAULT_UC);

  return scope.Close( Undefined() );
}

static void ToHtml(eio_req *req) {
  request *sr = static_cast<request *>(req->data);

  struct mkd_renderer renderer;
  struct buf *input_buf, *output_buf;

  // Create input buffer from string
  input_buf = bufnew(sr->in.size());
  bufput(input_buf, sr->in.c_str(), sr->in.size());

  // Create output buffer and reset size to 0
	output_buf = bufnew(OUTPUT_UNIT);
  output_buf->size = 0;

  // Use new Upskirt HTML to render
  upshtml_renderer(&renderer, 0);
  ups_markdown(output_buf, input_buf, &renderer, ~0);
  upshtml_free_renderer(&renderer);

  sr->out = output_buf->data;
  sr->out_size = output_buf->size;

	/* cleanup */
	bufrelease(input_buf);
  bufrelease(output_buf);
}

static int ToHtml_After(eio_req *req) {
  HandleScope scope;

  ev_unref(EV_DEFAULT_UC);
  request *sr = static_cast<request *>(req->data);
  Local<Value> argv[1];

  // Set contents
  const char* contents = sr->out.c_str();

  // Create and assign fast buffer to arguments array
  Buffer* buffer = Buffer::New(const_cast<char *>(contents), sr->out_size);
  Local<Object> fastBuffer;
  MAKE_FAST_BUFFER(buffer, fastBuffer);
  argv[0] = fastBuffer;

  // Invoke callback function with html argument
  TryCatch try_catch;
  sr->callback->Call(Context::GetCurrent()->Global(), 1, argv);
  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  /* cleanup */
  sr->callback.Dispose();
  free(sr);
  return 0;
}

static Handle<Value> ToHtmlSync(const Arguments &args) {
  HandleScope scope;

  const char *usage = "usage: toHtmlSync(markdown_string)";
  if (args.Length() < 1 || !args[0]->IsString()) {
    return ThrowException(Exception::TypeError(String::New(usage)));
  }

  struct mkd_renderer renderer;
  struct buf *input_buf, *output_buf;

  String::Utf8Value utf8_in(args[0]);
  string in = *utf8_in;
  string out;

  // Create input buffer from string
  input_buf = bufnew(in.size());
  bufput(input_buf, in.c_str(), in.size());

  // Create output buffer and reset size to 0
  output_buf = bufnew(OUTPUT_UNIT);
  output_buf->size = 0;

  // Use new Upskirt HTML to render
  upshtml_renderer(&renderer, 0);
  ups_markdown(output_buf, input_buf, &renderer, ~0);
  upshtml_free_renderer(&renderer);

  out = output_buf->data;

  // Create and assign fast buffer to arguments array
  Handle<String> md = String::New(output_buf->data, output_buf->size);

	/* cleanup */
  bufrelease(input_buf);
  bufrelease(output_buf);
  return scope.Close(md);
}
 
extern "C" void init (Handle<Object> target) {
  HandleScope scope;

  target->Set(String::New("version"), String::New("0.2.2"));
  NODE_SET_METHOD(target, "toHtml", ToHtmlAsync);
  NODE_SET_METHOD(target, "toHtmlSync", ToHtmlSync);
}
