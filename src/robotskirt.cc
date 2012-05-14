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

class Renderer : public ObjectWrap {
protected:
  mkd_renderer* const ptr;
public:
  inline Renderer() : ptr(new mkd_renderer) {}
  inline mkd_renderer* getHandle() {return ptr;}
  inline ~Renderer() {
    delete ptr;
  }
  static Handle<Value> newInstance(const Arguments& args);
};

class HtmlRenderer : public Renderer {
public:
  inline HtmlRenderer(unsigned int flags) {
    upshtml_renderer(ptr, flags);
  }
  inline ~HtmlRenderer() {
    upshtml_free_renderer(ptr);
  }
  static Handle<Value> newInstance(const Arguments& args);
};

Handle<Value> Renderer::newInstance(const Arguments& args) {
  HandleScope scope;

  (new Renderer)->Wrap(args.This());
  return scope.Close(args.This());
}

Handle<Value> HtmlRenderer::newInstance(const Arguments& args) {
  HandleScope scope;

  unsigned int flags = 0;
  if (args.Length() >= 1) flags = args[0]->Uint32Value(); //FIXME: introduce method, allow list of flags

  (new HtmlRenderer(flags))->Wrap(args.This());
  return scope.Close(args.This());
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
  try {
    //Check arguments
    if (args.Length() < 3 || (!args[0]->IsObject()) || (!args[2]->IsFunction()))
      throw Exception::TypeError(String::New("usage: markdown(renderer, input, callback, [extensions])"));

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
  } catch (Handle<Value> h) {
    return ThrowException(h);
  } catch (exception e) {
    return ThrowException(Exception::Error(String::New(e.what())));
  }
}

static void markdownProcess(eio_req *req) {
  //Extract request and renderer
  request *sr = static_cast<request *>(req->data);
  Renderer* rwrap = Renderer::Unwrap<Renderer>(sr->renderer);
  mkd_renderer* rend = rwrap->getHandle();

  // Create input buffer from string
  struct buf *input_buf = bufnew(sr->in.size());
//  try {FIXME: RAII
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
  sr->callback.Dispose(); //FIXME: should we put this into a finally (RAII)?
  return 0;
}

static Handle<Value> markdownSync(const Arguments &args) {
  HandleScope scope;
  try {
    //Check arguments
    if (args.Length() < 2 || (!args[0]->IsObject()))
      throw Exception::TypeError(String::New("usage: markdown(renderer, input, [extensions])"));

    //Extract arguments
    Renderer* rwrap = Renderer::Unwrap<Renderer>(args[0]->ToObject());
    mkd_renderer* rend = rwrap->getHandle();
    String::Utf8Value instr (args[1]);
    unsigned int flags = 0;
    if (args.Length() >= 3) flags = args[2]->Uint32Value(); //FIXME

    // Create input buffer from string
    struct buf *input_buf = bufnew(instr.length());
  //  try {FIXME: RAII
      bufput(input_buf, *instr, instr.length());

      // Create output buffer and reset size to 0
      struct buf *out = bufnew(OUTPUT_UNIT);
      out->size = 0;

      // Render!
      ups_markdown(out, input_buf, rend, flags);
      
      // Create and return fast buffer to result
      Buffer* buffer = Buffer::New(const_cast<char *>(out->data), out->size, outputBufferFree, out);
      Local<Object> fastBuffer;
      MAKE_FAST_BUFFER(buffer, fastBuffer);
  //  } finally {
      //Free input buffer
      bufrelease(input_buf);
  //  }
      return scope.Close(fastBuffer);
  } catch (Handle<Value> h) {
    return ThrowException(h);
  } catch (exception e) {
    return ThrowException(Exception::Error(String::New(e.what())));
  }
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
    Local<FunctionTemplate> rendL = FunctionTemplate::New(&Renderer::newInstance);
    Persistent<FunctionTemplate> rend = Persistent<FunctionTemplate>::New(rendL);
    rend->InstanceTemplate()->SetInternalFieldCount(1);
    rend->SetClassName(String::NewSymbol("Renderer"));

    target->Set(String::NewSymbol("Renderer"), rend->GetFunction());

    //Standard HTML renderer class
    Local<FunctionTemplate> htmlrendL = FunctionTemplate::New(&HtmlRenderer::newInstance);
    Persistent<FunctionTemplate> htmlrend = Persistent<FunctionTemplate>::New(htmlrendL);
    htmlrend->InstanceTemplate()->SetInternalFieldCount(1);
    htmlrend->Inherit(rend);
    htmlrend->SetClassName(String::NewSymbol("HtmlRenderer"));

    target->Set(String::NewSymbol("HtmlRenderer"), htmlrend->GetFunction());
  }
  NODE_MODULE(robotskirt, init)
}
