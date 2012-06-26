#include <v8.h>
#include <node.h>
#include <node_buffer.h>

#include <string>

#include "markdownWrapper.hpp"

using namespace std;

using namespace node;
using namespace v8;

using namespace mkd;

#define OUTPUT_UNIT 64


////////////////////////////////////////////////////////////////////////////////
// UTILITIES to ease wrapping and interfacing with V8
////////////////////////////////////////////////////////////////////////////////

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

// V8 exception wrapping

#define V8_WRAP_START()                                                        \
  HandleScope scope;                                                           \
  try {

#define V8_WRAP_END()                                                          \
  } catch (Handle<Value> err) {                                                \
    return ThrowException(err);                                                \
  } catch (exception err) {                                                    \
    return ThrowException(Exception::Error(String::New(err.what())));          \
  }

#define V8_WRAP_END_NR()                                                       \
  } catch (Handle<Value> err) {                                                \
    ThrowException(err);                                                       \
  } catch (exception err) {                                                    \
    ThrowException(Exception::Error(String::New(err.what())));                 \
  }

// Dealing with V8 persistent handles

template <class T> void ClearPersistent(Persistent<T>& handle) {
    if (handle.IsEmpty()) return;
    handle.Dispose();
    handle.Clear();
}

template <class T> void SetPersistent(Persistent<T>& handle, Handle<T> value) {
    ClearPersistent<T>(handle);
    if (value.IsEmpty()) return;
    handle = Persistent<T>::New(value);
}



////////////////////////////////////////////////////////////////////////////////
// MODULE DECLARATION
////////////////////////////////////////////////////////////////////////////////

extern "C" {
  void init (Handle<Object> target) {
    HandleScope scope;

    //Static functions & properties
    target->Set(String::NewSymbol("version"), String::New("1.0.0"));
    NODE_SET_METHOD(target, "markdown", markdown);
    NODE_SET_METHOD(target, "markdownSync", markdownSync);

    //Extension constants
    target->Set(String::NewSymbol("EXT_AUTOLINK"), Integer::New(MKDEXT_AUTOLINK));
    target->Set(String::NewSymbol("EXT_FENCED_CODE"), Integer::New(MKDEXT_FENCED_CODE));
    target->Set(String::NewSymbol("EXT_LAX_SPACING"), Integer::New(MKDEXT_LAX_SPACING));
    target->Set(String::NewSymbol("EXT_NO_INTRA_EMPHASIS"), Integer::New(MKDEXT_NO_INTRA_EMPHASIS));
    target->Set(String::NewSymbol("EXT_SPACE_HEADERS"), Integer::New(MKDEXT_SPACE_HEADERS));
    target->Set(String::NewSymbol("EXT_STRIKETHROUGH"), Integer::New(MKDEXT_STRIKETHROUGH));
    target->Set(String::NewSymbol("EXT_SUPERSCRIPT"), Integer::New(MKDEXT_SUPERSCRIPT));
    target->Set(String::NewSymbol("EXT_TABLES"), Integer::New(MKDEXT_TABLES));

    //TODO: html renderer flags
    
    //TODO
  }
  NODE_MODULE(robotskirt, init)
}
