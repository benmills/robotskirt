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

struct request {
  Persistent<Function> callback;
  string in;
  buf *out; //Pass the output buffer directly
  unsigned int flags;
  Persistent<Object> renderer;
};

////////////////////////
// Renderer functions //
////////////////////////

static Handle<Value> newRenderer(const Arguments& args) {
  HandleScope scope;
  
  args.This()->SetPointerInInternalField(0, new mkd_renderer); //TODO: don't wrap mkd_renderer directly
  return args.This();
}

static Handle<Value> stdHtmlRenderer(const Arguments& args) {
  HandleScope scope;

  unsigned int flags = 0;
  if (args.Length() >= 1) flags = args[0]->Uint32Value(); //FIXME: introduce method, allow list of flags

  Local<Function> proto = Local<Function>::Cast(args.Data());
  Local<Object> obj = proto->NewInstance();
  mkd_renderer* rend = static_cast<mkd_renderer*>( obj->GetPointerFromInternalField(0) );

  upshtml_renderer(rend, flags);

  return scope.Close(obj);
}


//////////////////////
// Render functions //
//////////////////////

static void markdownProcess(eio_req *req);
static int markdownAfter(eio_req *req);

static void outputBufferFree(char* data, void* hint) {
  buf* b = static_cast<buf*>(hint);
  bufrelease(b);
}

static Handle<Value> markdown(const Arguments& args) {
  HandleScope scope;

  //Check arguments
  if (args.Length() < 3)
    return ThrowException(Exception::TypeError(String::New("usage: markdown(renderer, input, callback, [extensions])")));

  //Extract arguments
  Local<Object> rend = args[0]->ToObject();
  String::Utf8Value in(args[1]);
  Local<Function> callback = Local<Function>::Cast(args[2]);
  unsigned int flags = 0;
  if (args.Length() >= 4) flags = args[3]->Uint32Value(); //FIXME

  //Make request
  request *sr = new request;
  sr->callback = Persistent<Function>::New(callback);
  sr->in = *in;
  sr->flags = flags;
  sr->renderer = Persistent<Object>::New(rend);

  //Call process
  eio_custom(markdownProcess, EIO_PRI_DEFAULT, markdownAfter, sr);
  ev_ref(EV_DEFAULT_UC);

  return scope.Close( Undefined() );
}

static void markdownProcess(eio_req *req) {
  //Extract request and renderer
  request *sr = static_cast<request *>(req->data);
  void* rendptr = sr->renderer->GetPointerFromInternalField(0);
  mkd_renderer* rend = static_cast<mkd_renderer*>(rendptr);

  struct buf *input_buf;

  // Create input buffer from string
  input_buf = bufnew(sr->in.size());
//  try {
    bufput(input_buf, sr->in.c_str(), sr->in.size());

    // Create output buffer and reset size to 0
    sr->out = bufnew(OUTPUT_UNIT);
    sr->out->size = 0;

    // Render!
    ups_markdown(sr->out, input_buf, rend, sr->flags);
//  } finally {
    //Free input buffer
    bufrelease(input_buf);
//  }
}

static int markdownAfter(eio_req *req) {
  HandleScope scope;

  ev_unref(EV_DEFAULT_UC);
  request *sr = static_cast<request*>(req->data);
//  try {
    Local<Value> argv[1];

    // Create and assign fast buffer to arguments array
    Buffer* buffer = Buffer::New(const_cast<char *>(sr->out->data), sr->out->size, outputBufferFree, sr->out);
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
//  } finally {
    delete sr;
//  }
    return 0;
}

static Handle<Value> markdownSync(const Arguments &args) {
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


////////////////////////
// Module declaration //
////////////////////////

extern "C" {
  void init (Handle<Object> target) {
    HandleScope scope;

    //Static functions & properties
    target->Set(String::NewSymbol("version"), String::New("0.2.2"));
    NODE_SET_METHOD(target, "markdown", markdown);
    NODE_SET_METHOD(target, "markdownSync", markdownSync);

    //Extension constants
    target->Set(String::NewSymbol("EXT_AUTOLINK"), Integer::New(MKDEXT_AUTOLINK));
    target->Set(String::NewSymbol("EXT_FENCED_CODE"), Integer::New(MKDEXT_FENCED_CODE));
    target->Set(String::NewSymbol("EXT_LAX_HTML_BLOCKS"), Integer::New(MKDEXT_LAX_HTML_BLOCKS));
    target->Set(String::NewSymbol("EXT_NO_INTRA_EMPHASIS"), Integer::New(MKDEXT_NO_INTRA_EMPHASIS));
    target->Set(String::NewSymbol("EXT_SPACE_HEADERS"), Integer::New(MKDEXT_SPACE_HEADERS));
    target->Set(String::NewSymbol("EXT_STRIKETHROUGH"), Integer::New(MKDEXT_STRIKETHROUGH));
    target->Set(String::NewSymbol("EXT_TABLES"), Integer::New(MKDEXT_TABLES));

    //TODO: html renderer flags

    //Renderer class
    Local<FunctionTemplate> rendL = FunctionTemplate::New(&newRenderer);
    Persistent<FunctionTemplate> rend = Persistent<FunctionTemplate>::New(rendL);
    rend->InstanceTemplate()->SetInternalFieldCount(1);
    rend->SetClassName(String::NewSymbol("Renderer"));

    target->Set(String::NewSymbol("Renderer"), rend->GetFunction());

    //Standard HTML renderer factory function
    Local<FunctionTemplate> stdrendL = FunctionTemplate::New(&stdHtmlRenderer, rend->GetFunction());
    Persistent<FunctionTemplate> stdrend = Persistent<FunctionTemplate>::New(stdrendL);

    target->Set(String::NewSymbol("htmlRenderer"), stdrend->GetFunction());
  }
  NODE_MODULE(robotskirt, init)
}
