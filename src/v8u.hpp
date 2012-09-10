/*
 * V8Utilities - Sugar for your Node C++ addons
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

// V8 exception wrapping

#define V8_THROW(VALUE) throw v8::Persistent<v8::Value>::New(VALUE);

#define V8_WRAP_START()                                                        \
  v8::HandleScope scope;                                                       \
  try {

#define V8_WRAP_END()                                                          \
    return scope.Close(v8::Undefined());                                       \
  } catch (v8::Persistent<v8::Value>& err) {                                   \
    v8::Local<v8::Value> loc = v8::Local<v8::Value>::New(err);                 \
    err.Dispose();                                                             \
    return ThrowException(loc);                                                \
  } catch (v8::Handle<v8::Value>& err) {                                       \
    return ThrowException(err);                                                \
  } catch (v8::Value*& err) {                                                  \
    return ThrowException(v8::Handle<v8::Value>(err));                         \
  } catch (std::exception& err) {                                              \
    return ThrowException(v8::Exception::Error(v8::String::New(err.what())));  \
  } catch (std::string& err) {                                                 \
    return ThrowException(v8::Exception::Error(v8::String::New(err.data(), err.length())));\
  } catch (...) {                                                              \
    return ThrowException(v8::Exception::Error(v8::String::New("Unknown error!")));\
  }

#define V8_WRAP_END_NR()                                                       \
  } catch (v8::Persistent<v8::Value>& err) {                                   \
    v8::Local<v8::Value> loc = v8::Local<v8::Value>::New(err);                 \
    err.Dispose();                                                             \
    ThrowException(loc);                                                       \
  } catch (v8::Handle<v8::Value>& err) {                                       \
    ThrowException(err);                                                       \
  } catch (v8::Value*& err) {                                                  \
    ThrowException(v8::Handle<v8::Value>(err));                                \
  } catch (std::exception& err) {                                              \
    ThrowException(v8::Exception::Error(v8::String::New(err.what())));         \
  } catch (std::string& err) {                                                 \
    ThrowException(v8::Exception::Error(v8::String::New(err.data(), err.length())));\
  } catch (...) {                                                              \
    ThrowException(v8::Exception::Error(v8::String::New("Unknown error!")));   \
  }

// JS arguments

inline void CheckArguments(int min, const v8::Arguments& args) {
  if (args.Length() < min)
    throw v8::Persistent<v8::Value>::New(v8::Exception::RangeError(v8::String::New("Not enough arguments.")));
}

// V8 callback templates

#define V8_S_CALLBACK(IDENTIFIER)                                              \
  v8::Handle<v8::Value> IDENTIFIER(const v8::Arguments& args)

#define V8_CALLBACK(IDENTIFIER)                                                \
V8_S_CALLBACK(IDENTIFIER) {                                                    \
  V8_WRAP_START()

#define V8_CALLBACK_END() V8_WRAP_END() }

#define V8_S_GETTER(IDENTIFIER)                                                \
  v8::Handle<v8::Value> IDENTIFIER(v8::Local<v8::String> name,                 \
                                   const v8::AccessorInfo& info)

#define V8_GETTER(IDENTIFIER)                                                  \
V8_S_GETTER(IDENTIFIER) {                                                      \
  V8_WRAP_START()

#define V8_GETTER_END() V8_WRAP_END() }

#define V8_S_SETTER(IDENTIFIER)                                                \
  void IDENTIFIER(v8::Local<v8::String> name, v8::Local<v8::Value> value,      \
                  const v8::AccessorInfo& info)

#define V8_SETTER(IDENTIFIER)                                                  \
V8_S_SETTER(IDENTIFIER) {                                                      \
  V8_WRAP_START()

#define V8_SETTER_END() V8_WRAP_END_NR() }

#define V8_UNWRAP(CPP_TYPE, OBJ)                                               \
  CPP_TYPE* inst = node::ObjectWrap::Unwrap<CPP_TYPE>(OBJ.Holder());

// Class-specific templates

#define V8_CL_CTOR(CPP_TYPE)                                                   \
static V8_S_CALLBACK(NewInstance) {                                            \
  if ((args.Length()==1) && (args[0]->IsExternal())) {                         \
    ((CPP_TYPE*)v8::External::Unwrap(args[0]))->Wrap(args.This());             \
    return args.This();                                                        \
  }                                                                            \
  if (!args.IsConstructCall())                                                 \
    return v8::ThrowException(v8u::ReferenceErr("You must call this as a constructor"));\
  V8_WRAP_START()                                                              \
  CPP_TYPE* inst;

#define V8_CL_CTOR_END()                                                       \
  inst->Wrap(args.This());                                                     \
  return scope.Close(args.This());                                             \
V8_CALLBACK_END()

#define V8_CL_GETTER(CPP_TYPE, CPP_VAR)                                        \
  static V8_GETTER(Getter__##CPP_VAR)                                          \
    V8_UNWRAP(CPP_TYPE, info)

#define V8_CL_SETTER(CPP_TYPE, CPP_VAR)                                        \
  static V8_SETTER(Setter__##CPP_VAR)                                          \
    V8_UNWRAP(CPP_TYPE, info)

#define V8_CL_CALLBACK(CPP_TYPE, IDENTIFIER)                                   \
  static V8_CALLBACK(IDENTIFIER)                                               \
    V8_UNWRAP(CPP_TYPE, args)

#define V8_CL_WRAPPER(CLASSNAME)                                               \
  /**
   * Returns the unique V8 v8::Object corresponding to this C++ instance.
   * For this to work, you should use V8_CONSTRUCTOR.
   *
   * CALLING Wrapped() WITHIN A CONSTRUCTOR MAY YIELD UNEXPECTED RESULTS,
   * EVENTUALLY MAKING YOU BASH YOUR HEAD AGAINST A WALL. YOU HAVE BEEN WARNED.
   **/                                                                         \
  virtual v8::Local<v8::Object> Wrapped() {                                    \
    v8::HandleScope scope;                                                     \
                                                                               \
    if (handle_.IsEmpty()) {                                                   \
      v8::Handle<v8::Value> args [1] = {v8::External::New(this)};              \
      v8u::GetTemplate(CLASSNAME)->GetFunction()->NewInstance(1,args);         \
    }                                                                          \
    return scope.Close(handle_);                                               \
  }

// Dealing with V8 persistent handles

template <class T> inline void ClearPersistent(v8::Persistent<T>& handle) {
  if (handle.IsEmpty()) return;
  handle.Dispose();
  handle.Clear();
}

template <class T> inline void SetPersistent(v8::Persistent<T>& handle, v8::Handle<T> value) {
  ClearPersistent<T>(handle);
  if (value.IsEmpty()) return;
  handle = v8::Persistent<T>::New(value);
}

inline v8::Persistent<v8::Value> Persist(v8::Handle<v8::Value> handle) {
  return v8::Persistent<v8::Value>::New(handle);
}

template <class T> class Persisted {
public:
  inline Persisted() {};
  inline Persisted(v8::Handle<T> value) : handle(v8::Persistent<T>::New(value)) {}
  inline ~Persisted() {
    if (!handle.IsEmpty()) handle.Dispose();
  }
  inline Persisted(Persisted<T>& other) : handle(v8::Persistent<T>::New(other.handle)) {}
  inline v8::Persistent<T> operator*() const {
    return handle;
  }
  inline Persisted<T>& operator=(const Persisted<T>& other) {
    if (&other == this) return *this;
    SetPersistent<T>(handle, other.handle);
    return *this;
  }
  inline bool operator==(const Persisted<T>& other) const {
    return handle==other.handle;
  }
  inline bool IsEmpty() const {
    return handle.IsEmpty();
  }
  inline void Clear() {
    ClearPersistent<T>(handle);
  }
  inline T* operator->() const {
    return *handle;
  }
private:
  v8::Persistent<T> handle;
};

// Type shortcuts

inline v8::Local<v8::Integer> Int(int64_t integer) {
  return v8::Integer::New(integer);
}

inline v8::Local<v8::Integer> Uint(uint32_t integer) {
  return v8::Integer::NewFromUnsigned(integer);
}

inline v8::Local<v8::String> Str(const char* data) {
  return v8::String::New(data);
}

inline v8::Local<v8::String> Str(std::string str) {
  return v8::String::New(str.data(), str.length());
}

inline v8::Local<v8::String> Symbol(const char* data) {
  return v8::String::NewSymbol(data);
}

inline v8::Local<v8::Object> Obj() {
  return v8::Object::New();
}

#define __V8_ERROR_CTOR(ERROR)                                                 \
inline v8::Local<v8::Value> ERROR##Err(const char* msg) {                      \
  return v8::Exception::ERROR##Error(v8::String::New(msg));                    \
}

__V8_ERROR_CTOR()
__V8_ERROR_CTOR(Range)
__V8_ERROR_CTOR(Reference)
__V8_ERROR_CTOR(Syntax)
__V8_ERROR_CTOR(Type)

inline v8::Local<v8::Number> Num(double number) {
  return v8::Number::New(number);
}

inline v8::Handle<v8::Boolean> Bool(bool boolean) {
  return v8::Boolean::New(boolean);
}

inline v8::Local<v8::FunctionTemplate> Func(v8::InvocationCallback function) {
  return v8::FunctionTemplate::New(function);
}

// Type casting/unwraping shortcuts

inline double Num(v8::Handle<v8::Value> hdl) {
  return hdl->NumberValue();
}

inline int32_t Int(v8::Handle<v8::Value> hdl) {
  return hdl->Int32Value();
}

inline uint32_t Uint(v8::Handle<v8::Value> hdl) {
  return hdl->Uint32Value();
}

inline v8::Local<v8::Object> Obj(v8::Local<v8::Value> hdl) {
  return v8::Local<v8::Object>::Cast(hdl);
}

inline bool Bool(v8::Handle<v8::Value> hdl) {
  return hdl->BooleanValue();
}

// Defining things

#define V8_DEF_TYPE(V8_NAME)                                                   \
  v8::Persistent<v8::FunctionTemplate> prot = v8::Persistent<v8::FunctionTemplate>::New(\
      v8::FunctionTemplate::New(NewInstance));                                 \
  v8::Handle<v8::String> __cname = v8::String::NewSymbol(V8_NAME);             \
  prot->SetClassName(__cname);                                                 \
  prot->InstanceTemplate()->SetInternalFieldCount(1);

#define V8_DEF_PROP(CPP_VAR, V8_NAME)                                          \
  prot->InstanceTemplate()->SetAccessor(NODE_PSYMBOL(V8_NAME), Getter__##CPP_VAR, Setter__##CPP_VAR);

#define V8_DEF_RPROP(CPP_VAR, V8_NAME)                                         \
  prot->InstanceTemplate()->SetAccessor(NODE_PSYMBOL(V8_NAME), Getter__##CPP_VAR);

#define V8_DEF_METHOD(CPP_METHOD, V8_NAME)                                     \
  NODE_SET_PROTOTYPE_METHOD(prot, V8_NAME, CPP_METHOD);

#define V8_INHERIT(CLASSNAME) prot->Inherit(v8u::GetTemplate(CLASSNAME));

// Templates for definition methods on Node

#define NODE_DEF(IDENTIFIER)                                                   \
  void IDENTIFIER(v8::Handle<v8::Object> target)

#define NODE_DEF_TYPE(V8_NAME)                                                 \
  inline static NODE_DEF(init) {                                               \
    v8::HandleScope scope;                                                     \
    V8_DEF_TYPE(V8_NAME)

#define NODE_DEF_TYPE_END()                                                    \
    target->Set(__cname, prot->GetFunction());                                 \
  }

#define NODE_DEF_MAIN()                                                        \
  extern "C" {                                                                 \
    NODE_DEF(init) {                                                           \
      v8::HandleScope scope;

#define NODE_DEF_MAIN_END(MODULE) }                                            \
    NODE_MODULE(MODULE, init); }

// Storing templates for later use

class map_comparison {
public:
  bool operator()(const std::pair<v8::Handle<v8::Context>, std::string> a, const std::pair<v8::Handle<v8::Context>, std::string> b) {
  //Compare strings
    int cp = a.second.compare(b.second);
    if (cp) return cp < 0;

    //Compare contexts
    if (*a.first == NULL) return *b.first;
    if (*b.first == NULL) return false;
    return *((v8::internal::Object**)*a.first) < *((v8::internal::Object**)*b.first);
  }
};

//FIXME LOW PRIORITY: initialize at functions
std::map< std::pair<v8::Handle<v8::Context>, std::string>, v8::Persistent<v8::FunctionTemplate>,
    map_comparison> v8_wrapped_prototypes;

void StoreTemplate(std::string classname, v8::Persistent<v8::FunctionTemplate> templ) {
  v8::HandleScope scope;
  //FIXME, LOW PRIORITY: make a weak ref, ensure removal when context deallocates
  std::pair<v8::Handle<v8::Context>, std::string> key (v8::Persistent<v8::Context>::New(v8::Context::GetCurrent()), classname);
  v8_wrapped_prototypes.insert(
      std::pair< std::pair<v8::Handle<v8::Context>, std::string>,
                 v8::Persistent<v8::FunctionTemplate> > (key, templ)
  );
}

v8::Persistent<v8::FunctionTemplate> GetTemplate(std::string classname) {
  v8::HandleScope scope;
  std::pair<v8::Handle<v8::Context>, std::string> key (v8::Context::GetCurrent(), classname);
  return v8_wrapped_prototypes.at(key);
}

};

#endif	/* V8U_HPP */

