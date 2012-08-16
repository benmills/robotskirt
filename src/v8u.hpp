/* 
 * V8Utilities - Sugar for Node C/C++ addons
 * Copyright (c) 2012, Xavier Mendez
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef V8U_HPP
#define	V8U_HPP

#include <string>
#include <exception>
#include <map>

#include <node.h>
#include <v8.h>


namespace v8u {

using namespace v8;
using namespace node;
using namespace std;

// V8 exception wrapping

#define V8_THROW(VALUE) throw Persistent<Value>::New(VALUE);

#define V8_WRAP_START()                                                        \
  HandleScope scope;                                                           \
  try {

#define V8_WRAP_END()                                                          \
    return scope.Close(Undefined());                                           \
  } catch (Persistent<Value>& err) {                                           \
    Local<Value> loc = Local<Value>::New(err);                                 \
    err.Dispose();                                                             \
    return ThrowException(loc);                                                \
  } catch (Handle<Value>& err) {                                               \
    return ThrowException(err);                                                \
  } catch (Value*& err) {                                                      \
    return ThrowException(Handle<Value>(err));                                 \
  } catch (exception& err) {                                                   \
    return ThrowException(Exception::Error(String::New(err.what())));          \
  } catch (string& err) {                                                      \
    return ThrowException(Exception::Error(String::New(err.data(), err.length())));\
  } catch (...) {                                                              \
    return ThrowException(Exception::Error(String::New("Unknown error!")));    \
  }                                                                            \
}

#define V8_WRAP_END_NR()                                                       \
  } catch (Persistent<Value>& err) {                                           \
    Local<Value> loc = Local<Value>::New(err);                                 \
    err.Dispose();                                                             \
    ThrowException(loc);                                                       \
  } catch (Handle<Value>& err) {                                               \
    ThrowException(err);                                                       \
  } catch (Value*& err) {                                                      \
    ThrowException(Handle<Value>(err));                                        \
  } catch (exception& err) {                                                   \
    ThrowException(Exception::Error(String::New(err.what())));                 \
  } catch (string& err) {                                                      \
    ThrowException(Exception::Error(String::New(err.data(), err.length())));   \
  } catch (...) {                                                              \
    ThrowException(Exception::Error(String::New("Unknown error!")));           \
  }                                                                            \
}

// JS arguments

void CheckArguments(int min, const Arguments& args) {
  if (args.Length() < min)
    throw Persistent<Value>::New(Exception::RangeError(String::New("Not enough arguments.")));
}

// V8 callback templates

#define V8_CALLBACK(IDENTIFIER, MIN)                                           \
Handle<Value> IDENTIFIER(const Arguments& args) {                              \
  V8_WRAP_START()                                                              \
    CheckArguments(MIN, args);

#define V8_GETTER(IDENTIFIER)                                                  \
Handle<Value> IDENTIFIER(Local<String> name, const AccessorInfo& info) {       \
  V8_WRAP_START()

#define V8_SETTER(IDENTIFIER)                                                  \
void IDENTIFIER(Local<String> name, Local<Value> value,                        \
                const AccessorInfo& info) {                                    \
  V8_WRAP_START()

#define V8_UNWRAP(CPP_TYPE, OBJ)                                               \
  CPP_TYPE* inst = ObjectWrap::Unwrap<CPP_TYPE>(OBJ.Holder());

// Class-specific templates

#define V8_CL_CTOR(CPP_TYPE, MIN)                                              \
static Handle<Value> NewInstance(const Arguments& args) {                      \
  V8_WRAP_START()                                                              \
  if ((args.Length()==1) && (args[0]->IsExternal())) {                         \
    ((CPP_TYPE*)External::Unwrap(args[0]))->Wrap(args.This());                 \
    return scope.Close(args.This());                                           \
  }                                                                            \
  if (!args.IsConstructCall())                                                 \
    throw Persistent<Value>::New(Exception::ReferenceError(String::New("You must call this as a constructor")));\
  CheckArguments(MIN, args);                                                   \
  CPP_TYPE* inst;

#define V8_CL_CTOR_END()                                                       \
  inst->Wrap(args.This());                                                     \
  return scope.Close(args.This());                                             \
V8_WRAP_END()

#define V8_CL_GETTER(CPP_TYPE, CPP_VAR)                                        \
  static V8_GETTER(Getter__##CPP_VAR)                                          \
    V8_UNWRAP(CPP_TYPE, info)

#define V8_CL_SETTER(CPP_TYPE, CPP_VAR)                                        \
  static V8_SETTER(Setter__##CPP_VAR)                                          \
    V8_UNWRAP(CPP_TYPE, info)

#define V8_CL_CALLBACK(CPP_TYPE, IDENTIFIER, MIN)                              \
  static V8_CALLBACK(IDENTIFIER, MIN)                                          \
    V8_UNWRAP(CPP_TYPE, args)

#define V8_CL_WRAPPER(CLASSNAME)                                               \
  /**
   * Returns the unique V8 Object corresponding to this C++ instance.
   * For this to work, you should use V8_CONSTRUCTOR.
   *
   * CALLING Wrapped() WITHIN A CONSTRUCTOR MAY YIELD UNEXPECTED RESULTS,
   * EVENTUALLY MAKING YOU BASH YOUR HEAD AGAINST A WALL. YOU HAVE BEEN WARNED.
   **/                                                                         \
  virtual Local<Object> Wrapped() {                                            \
    HandleScope scope;                                                         \
                                                                               \
    if (handle_.IsEmpty()) {                                                   \
      Handle<Value> args [1] = {External::New(this)};                          \
      GetTemplate(CLASSNAME)->GetFunction()->NewInstance(1,args);              \
    }                                                                          \
    return scope.Close(handle_);                                               \
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

Persistent<Value> Persist(Handle<Value>& handle) {
  return Persistent<Value>::New(handle);
}

// Type shortcuts

Local<Integer> Int(int32_t integer) {
  return Integer::New(integer);
}

Local<Integer> Uint(uint32_t integer) {
  return Integer::NewFromUnsigned(integer);
}

Local<String> Str(const char* data) {
  return String::New(data);
}

Local<String> Str(string str) {
  return String::New(str.data(), str.length());
}

Local<String> Symbol(const char* data) {
  return String::NewSymbol(data);
}

Local<Object> Obj() {
  return Object::New();
}

#define __V8_ERROR_CTOR(ERROR)                                                 \
Local<Value> ERROR##Err(const char* msg) {                                     \
  return Exception::ERROR##Error(String::New(msg));                            \
}

__V8_ERROR_CTOR()
__V8_ERROR_CTOR(Range)
__V8_ERROR_CTOR(Reference)
__V8_ERROR_CTOR(Syntax)
__V8_ERROR_CTOR(Type)

Local<Number> Num(double number) {
  return Number::New(number);
}

Handle<Boolean> Bool(bool boolean) {
  return Boolean::New(boolean);
}

Local<FunctionTemplate> Func(InvocationCallback function) {
  return FunctionTemplate::New(function);
}

// Type casting/unwraping shortcuts

double Num(Local<Value> hdl) {
  return hdl->NumberValue();
}

int64_t Int(Local<Value> hdl) {
  return hdl->IntegerValue();
}

uint32_t Uint(Local<Value> hdl) {
  return hdl->Uint32Value();
}

Local<Object> Obj(Local<Value> hdl) {
  return Local<Object>::Cast(hdl);
}

bool Bool(Local<Value> hdl) {
  return hdl->BooleanValue();
}

// Defining things

#define V8_DEF_TYPE(CPP_TYPE, V8_NAME)                                         \
  Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(       \
      FunctionTemplate::New(CPP_TYPE::NewInstance));                           \
  Handle<String> __cname = String::NewSymbol(V8_NAME);                         \
  prot->SetClassName(__cname);                                                 \
  prot->InstanceTemplate()->SetInternalFieldCount(1);

#define V8_DEF_PROP(CPP_TYPE, CPP_VAR, V8_NAME)                                \
  prot->InstanceTemplate()->SetAccessor(NODE_PSYMBOL(V8_NAME), CPP_TYPE::Getter__##CPP_VAR, CPP_TYPE::Setter__##CPP_VAR);

#define V8_DEF_RPROP(CPP_TYPE, CPP_VAR, V8_NAME)                               \
  prot->InstanceTemplate()->SetAccessor(NODE_PSYMBOL(V8_NAME), CPP_TYPE::Getter__##CPP_VAR);

#define V8_DEF_METHOD(CPP_TYPE, CPP_METHOD, V8_NAME)                           \
  NODE_SET_PROTOTYPE_METHOD(prot, V8_NAME, CPP_TYPE::CPP_METHOD);

#define V8_INHERIT(CLASSNAME) prot->Inherit(GetTemplate(CLASSNAME));

// Templates for definition methods on Node

#define NODE_DEF(IDENTIFIER)                                                   \
  void IDENTIFIER(Handle<Object> target)

#define NODE_DEF_TYPE(CPP_TYPE, V8_NAME)                                       \
  NODE_DEF(init##CPP_TYPE) {                                                   \
    HandleScope scope;                                                         \
    V8_DEF_TYPE(CPP_TYPE, V8_NAME)

#define NODE_DEF_TYPE_END()                                                    \
    target->Set(__cname, prot->GetFunction());                                 \
  }

#define NODE_DEF_MAIN()                                                        \
  extern "C" {                                                                 \
    NODE_DEF(init) {                                                           \
      HandleScope scope;

#define NODE_DEF_MAIN_END(MODULE) }                                            \
    NODE_MODULE(MODULE, init); }

// Storing templates for later use

class map_comparison {
public:
  bool operator()(const pair<Handle<Context>, string> a, const pair<Handle<Context>, string> b) {
  //Compare strings
    int cp = a.second.compare(b.second);
    if (cp) return cp < 0;

    //Compare contexts
    if (*a.first == NULL) return *b.first;
    if (*b.first == NULL) return false;
    return *((internal::Object**)*a.first) < *((internal::Object**)*b.first);
  }
};

map< pair<Handle<Context>, string>, Persistent<FunctionTemplate>,
    map_comparison> v8_wrapped_prototypes;

void StoreTemplate(string classname, Persistent<FunctionTemplate> templ) {
  HandleScope scope;
  //FIXME, LOW PRIORITY: make a weak ref, ensure removal when context deallocates
  pair<Handle<Context>, string> key (Persistent<Context>::New(Context::GetCurrent()), classname);
  v8_wrapped_prototypes.insert(
      make_pair< pair<Handle<Context>, string>,
                 Persistent<FunctionTemplate> > (key, templ)
  );
}

Persistent<FunctionTemplate> GetTemplate(string classname) {
  HandleScope scope;
  pair<Handle<Context>, string> key (Context::GetCurrent(), classname);
  return v8_wrapped_prototypes.at(key);
}

};

#endif	/* V8U_HPP */

