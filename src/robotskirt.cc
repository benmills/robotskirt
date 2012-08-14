#include <v8.h>
#include <node.h>
#include <node_buffer.h>

#include <string>
#include <sstream>
#include <map>

#include "markdownWrapper.hpp" //FIXME: no longer needed, inline it

using namespace std;

using namespace node;
using namespace v8;

using namespace mkd;

// Constants taken from the official executable
#define OUTPUT_UNIT 64
#define DEFAULT_MAX_NESTING 16

namespace robotskirt {

////////////////////////////////////////////////////////////////////////////////
// UTILITIES to ease wrapping and interfacing with V8: FIXME should be taken to separate HPP
////////////////////////////////////////////////////////////////////////////////

// Credit: @samcday
// http://sambro.is-super-awesome.com/2011/03/03/creating-a-proper-buffer-in-a-node-c-addon/ 
#define MAKE_FAST_BUFFER(NG_SLOW_BUFFER, NG_FAST_BUFFER)      \
  Local<Function> NG_JS_BUFFER = Local<Function>::Cast(       \
    Context::GetCurrent()->Global()->Get(                     \
      String::New("Buffer")));                                \
                                                              \
  Handle<Value> NG_JS_ARGS[2] = {                             \
    NG_SLOW_BUFFER->handle_,                                  \
    Integer::New(Buffer::Length(NG_SLOW_BUFFER))/*,           \
    Integer::New(0) <- WITH THIS WILL THROW AN ERROR*/        \
  };                                                          \
                                                              \
  NG_FAST_BUFFER = NG_JS_BUFFER->NewInstance(2, NG_JS_ARGS);

// V8 exception wrapping

#define type_error(message) \
    throw Persistent<Value>::New(Exception::TypeError(String::New(message)));

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
    HandleScope scope;
    if (args.Length() < min)
        throw Exception::RangeError(String::New("Not enough arguments."));
}

int64_t CheckInt(Handle<Value> value) {
    HandleScope scope;//FIXME: don't allow floating-point values
    if (!value->IsNumber()) type_error("You must provide an integer!");
    return value->IntegerValue();
}

unsigned int CheckFlags(Handle<Value> hdl) {
    HandleScope scope;
    if (hdl->IsArray()) {
        unsigned int ret = 0;
        Handle<Array> array = Handle<Array>::Cast(hdl);
        for (uint32_t i=0; i<array->Length(); i++)
            ret |= CheckInt(array->Get(i));
        return ret;
    }
    return CheckInt(hdl);
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

#define V8_CONSTRUCTOR(TYPE, MIN)                                              \
static Handle<Value> NewInstance(const Arguments& args) {                      \
  V8_WRAP_START()                                                              \
  if ((args.Length()==1) && (args[0]->IsExternal())) {                         \
    ((TYPE*)External::Unwrap(args[0]))->Wrap(args.This());                     \
    return scope.Close(args.This());                                           \
  }                                                                            \
  if (!args.IsConstructCall())                                                 \
    throw Persistent<Value>::New(Exception::ReferenceError(String::New("You must call this as a constructor.")));\
  CheckArguments(MIN, args);                                                   \
  TYPE* instance;

#define V8_CONSTRUCTOR_END()                                                   \
  instance->Wrap(args.This());                                                 \
  return scope.Close(args.This());                                             \
V8_WRAP_END()

#define V8_WRAPPER(CLASSNAME)                                                  \
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



////////////////////////////////////////////////////////////////////////////////
// RENDER FUNCTIONS STUFF
////////////////////////////////////////////////////////////////////////////////

// SIGNATURES
enum CppSignature {
    void_BUF1,
    void_BUF2,
    void_BUF2INT,
    void_BUF3,
     int_BUF2,
     int_BUF2INT,
     int_BUF4,
     int_BUF1
};

// CONVERTERS (especially buf* to Local<Object>)

Local<Object> toBuffer(const buf* buf) {
    HandleScope scope;
    Local<Object> ret;
    Buffer* buffer = Buffer::New((char*)buf->data, buf->size);
    MAKE_FAST_BUFFER(buffer, ret);
    return scope.Close(ret);
}
void setToBuf(buf* target, Handle<Object> obj) { //FIXME: test, this can lead to memory leaks
    bufreset(target);
    target->data = (uint8_t*)Buffer::Data(obj);
    target->asize = target->size = Buffer::Length(obj);
    Buffer::Initialize(obj);
}
void putToBuf(buf* target, Handle<Object> obj) { //FIXME: test, this can lead to memory leaks
    bufput(target, (uint8_t*)Buffer::Data(obj), Buffer::Length(obj));
}

// FUNCTION DATA (this gets injected into CPP functions converted to JS)
class FunctionData: public ObjectWrap {
public:
    FunctionData(void* function, CppSignature signature, void* opaque):
            function_(function), signature_(signature), opaque_(opaque) {
    }
    ~FunctionData() {}
    void* getFunction() {return function_;}
    void* getOpaque() {return opaque_;}
    CppSignature getSignature() {return signature_;}
    static V8_CALLBACK(ToString, 0) {
        return scope.Close(String::New("<Native function>"));
    } V8_WRAP_END()
protected://TODO: add a shared_ptr to avoid opaque data deallocation
    void* const function_;
    CppSignature const signature_;
    void* const opaque_;
friend void setCppFunction(void*& func, Handle<Object> obj, CppSignature sig);
friend void jsFunction(Persistent<Object>& handle, void* func, CppSignature sig, InvocationCallback wrapper, void* opaque);
};

// CONVERT BETWEEN CPP AND JS FUNCTIONS

bool setCppFunction(void** func, void** opaque, Handle<Object> obj, CppSignature sig) {
    HandleScope scope;
    if (obj->InternalFieldCount() < 1) return false;
    if (obj->GetPointerFromInternalField(0) == NULL) return false; //FIXME
    FunctionData* data = ObjectWrap::Unwrap<FunctionData>(obj);
    if (sig != data->getSignature()) return false;
    *func = data->getFunction();
    *opaque = data->getOpaque();
    return true;
}

void jsFunction(Persistent<Object>& handle, void* func, CppSignature sig, InvocationCallback wrapper, void* opaque) {
    HandleScope scope;
    Local<ObjectTemplate> wrap = ObjectTemplate::New();
    wrap->SetInternalFieldCount(1);
    Local<FunctionTemplate> repr = FunctionTemplate::New(FunctionData::ToString);
    wrap->Set(String::NewSymbol("toString"), repr);
    wrap->Set(String::NewSymbol("inspect"), repr);
    wrap->SetCallAsFunctionHandler(wrapper);
    SetPersistent<Object>(handle, wrap->NewInstance());
    (new FunctionData(func,sig,opaque))->Wrap(handle);
}

//What to do if no function set?
#define NULL_ACTION()                                                          \
    type_error("No function was set for this action.");

#define NULL_ACTION_R() NULL_ACTION()

// WRAPPERS (call a [wrapped] CPP function from JS)

#define WRAPPER_CALL_void()
#define WRAPPER_CALL_int() int v =

#define WRAPPER_POST_CALL_void()
#define WRAPPER_POST_CALL_int()                                                \
    if (!v) return scope.Close(False());

#define BUF1_WRAPPER(CPPFUNC, RET)                                             \
    static V8_CALLBACK(CPPFUNC##_wrapper, 0) {                                 \
        FunctionData *data = Unwrap<FunctionData>(args.Holder());              \
                                                                               \
        BufWrap ob (bufnew(OUTPUT_UNIT));                                      \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, void*))data->getFunction())        \
                (*ob,  data->getOpaque());                                     \
        WRAPPER_POST_CALL_##RET()                                              \
        Local<Object> ret = toBuffer(*ob);                                     \
        ob->data = NULL;                                                       \
        return scope.Close(ret);                                               \
    } V8_WRAP_END()

// BINDERS (call a JS function from CPP)

#define BINDER_RETURN_void
#define BINDER_RETURN_int  1

#define BINDER_PRE_ERROR_void()
#define BINDER_PRE_ERROR_int()                                                 \
    if (!ret->BooleanValue()) return 0;

#define BUF1_BINDER(CPPFUNC, RET)                                              \
    static RET CPPFUNC##_binder(struct buf *ob, void *opaque) {                \
        HandleScope scope;                                                     \
                                                                               \
        Persistent<Object>& obj = ((RendererWrap*)opaque)->CPPFUNC;            \
        if (obj.IsEmpty()) {                                                   \
            NULL_ACTION()                                                      \
        }                                                                      \
                                                                               \
        /*Convert arguments*/                                                  \
        Local<Value> args [0];                                                 \
                                                                               \
        /*Call it!*/                                                           \
        TryCatch trycatch;                                                     \
        Local<Value> ret = obj->CallAsFunction(Context::GetCurrent()->Global(), 0, args);\
        if (trycatch.HasCaught())                                              \
            throw Persistent<Value>::New(trycatch.Exception());                \
        /*Convert the result back*/                                            \
        if (Buffer::HasInstance(ret)) {                                        \
            putToBuf(ob, Handle<Object>::Cast(ret));                           \
            return BINDER_RETURN_##RET;                                        \
        }                                                                      \
        if (ret->IsString()) {                                                 \
            putToBuf(ob, Buffer::New(Local<String>::Cast(ret)));               \
            return BINDER_RETURN_##RET;                                        \
        }                                                                      \
        BINDER_PRE_ERROR_##RET()                                               \
        type_error("You should return a Buffer or a String");                  \
    }

// FORWARDERS (forward a Sundown call to its original C++ renderer)

#define BUF1_FORWARDER(CPPFUNC, RET)                                           \
    static RET CPPFUNC##_forwarder(struct buf *ob, void *opaque) {             \
        RendererWrap* rend = (RendererWrap*)opaque;                            \
        return ((RET(*)(struct buf *ob, void *opaque))rend->CPPFUNC##_orig)(   \
                ob,                                                            \
                rend->CPPFUNC##_opaque);                                       \
    }

//TODO

////////////////////////////////////////////////////////////////////////////////
// RENDERER CLASS DECLARATION
////////////////////////////////////////////////////////////////////////////////

// JS ACCESSORS

#define _RENDFUNC_GETTER(CPPFUNC)                                              \
    static V8_GETTER(CPPFUNC##_getter) {                                       \
        RendererWrap* inst = Unwrap<RendererWrap>(info.Holder());              \
        Persistent<Object> func = inst->CPPFUNC;                               \
        if (func.IsEmpty()) return scope.Close(Undefined());                   \
        return scope.Close(func);                                              \
    } V8_WRAP_END()

#define _RENDFUNC_SETTER(CPPFUNC, SIGNATURE)                                   \
    static V8_SETTER(CPPFUNC##_setter) {                                       \
        RendererWrap* inst = Unwrap<RendererWrap>(info.Holder());              \
        inst->rend.CPPFUNC = &CPPFUNC##_binder;                                \
        if (!value->BooleanValue()) {                                          \
            ClearPersistent<Object>(inst->CPPFUNC);                            \
            return;                                                            \
        }                                                                      \
        if (!value->IsObject()) type_error("Value must be a function!");       \
        Local<Object> obj = value->ToObject();                                 \
        if (!obj->IsCallable()) type_error("Value must be a function!");       \
                                                                               \
        if (setCppFunction(&(inst->CPPFUNC##_orig),                            \
                           &(inst->CPPFUNC##_opaque),                          \
                           obj, SIGNATURE))                                    \
            inst->rend.CPPFUNC = &CPPFUNC##_forwarder;                         \
        SetPersistent<Object>(inst->CPPFUNC, obj);                             \
    } V8_WRAP_END_NR()

#define _RENDFUNC_VAR(CPPFUNC)                                                 \
    Persistent<Object> CPPFUNC;                                                \
    void* CPPFUNC##_orig;                                                      \
    void* CPPFUNC##_opaque;

// All the macros together
#define RENDFUNC_DEF(CPPFUNC, SIGBASE, RET)                                    \
    protected: _RENDFUNC_VAR(CPPFUNC)                                          \
    public:                                                                    \
        _RENDFUNC_GETTER(CPPFUNC)                                              \
        _RENDFUNC_SETTER(CPPFUNC, RET##_##SIGBASE)                             \
                                                                               \
        SIGBASE##_FORWARDER(CPPFUNC,RET) SIGBASE##_BINDER(CPPFUNC,RET)         \
        SIGBASE##_WRAPPER(CPPFUNC,RET)

#define RENDFUNC_WRAP(CPPFUNC, SIGBASE, RET)                                   \
    CPPFUNC##_orig = (void*)rend.CPPFUNC;                                      \
    CPPFUNC##_opaque = opaque;                                                 \
    rend.CPPFUNC = &CPPFUNC##_forwarder;                                       \
    jsFunction(CPPFUNC, CPPFUNC##_orig, RET##_##SIGBASE, &CPPFUNC##_wrapper, opaque);

// Forward declaration, to make things work
class Markdown;

class RendererWrap: public ObjectWrap {
public:
    V8_WRAPPER("RendererWrap")
    RendererWrap() {}
    ~RendererWrap() {
        ClearPersistent<Object>(hrule);
    }
    V8_CONSTRUCTOR(RendererWrap, 0) {
        instance = new RendererWrap();
    } V8_CONSTRUCTOR_END()
protected:
    void wrap_functions(void* opaque) {
        RENDFUNC_WRAP(hrule, BUF1, void)
    }
    sd_callbacks rend;

friend class Markdown;
// Renderer functions
RENDFUNC_DEF(hrule, BUF1, void)
};

class HtmlRendererWrap: public RendererWrap {
public:
    V8_WRAPPER("HtmlRendererWrap")
    HtmlRendererWrap(unsigned int flags): flags_(flags), options() {//TODO: custom options
        sdhtml_renderer(&rend, &options, 0);
        wrap_functions(&options);
    }
    ~HtmlRendererWrap() {}
    V8_CONSTRUCTOR(HtmlRendererWrap, 0) {
        //Extract arguments
        unsigned int flags = 0;
        if (args.Length() >= 1) {
            flags = CheckFlags(args[0]);
        }

        instance = new HtmlRendererWrap(flags);
    } V8_CONSTRUCTOR_END()

    static V8_GETTER(GetFlags) {
        HtmlRendererWrap* inst = Unwrap<HtmlRendererWrap>(info.Holder());
        return scope.Close(Integer::NewFromUnsigned(inst->flags_));
    } V8_WRAP_END()
protected:
    unsigned int const flags_;
    html_renderopt options;//TODO add a virtual method getOpaque()
};



////////////////////////////////////////////////////////////////////////////////
// MARKDOWN CLASS DECLARATION
////////////////////////////////////////////////////////////////////////////////

class Markdown: public ObjectWrap {
public:
    V8_WRAPPER("Markdown")
    Markdown(RendererWrap* renderer, unsigned int extensions, size_t max_nesting):
            markdown(sd_markdown_new(extensions, max_nesting, &renderer->rend, renderer)),
            renderer_(renderer), max_nesting_(max_nesting), extensions_(extensions) {
        SetPersistent<Object>(renderer_handle, renderer->handle_);
    }
    ~Markdown() {
        ClearPersistent<Object>(renderer_handle);
        sd_markdown_free(markdown);
    }
    V8_CONSTRUCTOR(Markdown, 1) {
        //Check & extract arguments
        if (!args[0]->IsObject()) type_error("You must provide a Renderer!");
        Local<Object> obj = args[0]->ToObject();

        if (!GetTemplate("RendererWrap")->HasInstance(obj))
            type_error("You must provide a Renderer!");
        RendererWrap* rend = Unwrap<RendererWrap>(obj);

        unsigned int flags = 0;
        size_t max_nesting = DEFAULT_MAX_NESTING;
        if (args.Length()>=2) {
            flags = CheckFlags(args[1]);
            if (args.Length()>=3) {
                max_nesting = CheckInt(args[2]);
            }
        }

        instance = new Markdown(rend, flags, max_nesting);
    } V8_CONSTRUCTOR_END()

    static V8_GETTER(GetRenderer) {
        Markdown* inst = Unwrap<Markdown>(info.Holder());
        return scope.Close(inst->renderer_handle);
    } V8_WRAP_END()
    static V8_GETTER(GetMaxNesting) {
        Markdown* inst = Unwrap<Markdown>(info.Holder());
        return scope.Close(Integer::NewFromUnsigned(inst->max_nesting_));
    } V8_WRAP_END()
    static V8_GETTER(GetExtensions) {
        Markdown* inst = Unwrap<Markdown>(info.Holder());
        return scope.Close(Integer::NewFromUnsigned(inst->extensions_));
    } V8_WRAP_END()
    
    //And the most important function(s)...
    static V8_CALLBACK(RenderSync, 1) {
        Markdown* md = Unwrap<Markdown>(args.This());
        
        //Extract input
        Local<Value> arg = args[0];
        Handle<Object> obj;
        if (Buffer::HasInstance(arg)) obj = Local<Object>::Cast(arg);
        else if (arg->IsString()) obj = Buffer::New(Local<String>::Cast(arg));
        else type_error("You must provide a Buffer or a String!");
        char* data = Buffer::Data(obj);
        size_t length = Buffer::Length(obj);
        
        //Prepare
        BufWrap out (bufnew(OUTPUT_UNIT));
        
        //GO!!
        sd_markdown_render(*out, (uint8_t*)data, length, md->markdown);
        
        //Finish
        Local<Object> fastBuffer = toBuffer(*out);
        out->data = NULL;
        
        return scope.Close(fastBuffer);
    } V8_WRAP_END()
    //TODO: async version of Render(...)
protected:
    sd_markdown* const markdown;
    RendererWrap* const renderer_;
    size_t const max_nesting_;
    unsigned int const extensions_;
    Persistent<Object> renderer_handle;
};



////////////////////////////////////////////////////////////////////////////////
// HTML Renderer options
////////////////////////////////////////////////////////////////////////////////

//class TocData: public ObjectWrap {
//private:
//    html_renderopt* handle;
//    int current_level_, level_offset_;
//public:
//    int getHeaderCount() const {return handle.header_count;}
//    int getCurrentLevel() const {return current_level_;}
//    int getLevelOffset() const {return level_offset_;}
//
//    void setHeaderCount(int header_count) {handle.header_count=header_count;}
//    void setCurrentLevel(int current_level) {current_level_ = current_level;}
//    void setLevelOffset(int level_offset) {level_offset_ = level_offset;}
//    
//    TocData() {}
//    TocData(int header_count, int current_level, int level_offset) {
//        handle.header_count = header_count;
//        handle.current_level = current_level;
//        handle.level_offset = level_offset;
//    }
//    TocData(TocData& other) : handle(other.handle) {}
//    ~TocData() {}
//    
//    static V8_CALLBACK(New, 0) {
//        int arg0 = 0;
//        int arg1 = 0;
//        int arg2 = 0;
//        
//        //Extract arguments
//        if (args.Length() >= 1) {
//            arg0 = CheckInt(args[0]);
//            if (args.Length() >= 2) {
//                arg1 = CheckInt(args[1]);
//                if (args.Length() >= 3) {
//                    arg2 = CheckInt(args[2]);
//                }
//            }
//        }
//        
//        (new TocData(arg0, arg1, arg2))->Wrap(args.This());
//        return args.This();
//    } V8_WRAP_END()
//
//    //Getters
//    static V8_GETTER(GetHeaderCount) {
//        TocData& h = *(Unwrap<TocData>(info.Holder()));
//        return scope.Close(Integer::New(h.handle.header_count));
//    } V8_WRAP_END()
//    static V8_GETTER(GetCurrentLevel) {
//        TocData& h = *(Unwrap<TocData>(info.Holder()));
//        return scope.Close(Integer::New(h.current_level_));
//    } V8_WRAP_END()
//    static V8_GETTER(GetLevelOffset) {
//        TocData& h = *(Unwrap<TocData>(info.Holder()));
//        return scope.Close(Integer::New(h.level_offset_));
//    } V8_WRAP_END()
//
//    //Setters
//    static V8_SETTER(SetHeaderCount) {
//        TocData& h = *(Unwrap<TocData>(info.Holder()));
//        h.handle.header_count = CheckInt(value);
//    } V8_WRAP_END_NR()
//    static V8_SETTER(SetCurrentLevel) {
//        TocData& h = *(Unwrap<TocData>(info.Holder()));
//        h.current_level_ = CheckInt(value);
//    } V8_WRAP_END_NR()
//    static V8_SETTER(SetLevelOffset) {
//        TocData& h = *(Unwrap<TocData>(info.Holder()));
//        h.level_offset_ = CheckInt(value);
//    } V8_WRAP_END_NR()
//};

//class HtmlOptions: public ObjectWrap {
//private:
//    unsigned int flags = 0;
//public:
//    
//};



////////////////////////////////////////////////////////////////////////////////
// OTHER JS FUNCTIONS (Version, ...)
////////////////////////////////////////////////////////////////////////////////

class Version: public ObjectWrap {
public:
    V8_WRAPPER("Version")
    Version(int major, int minor, int revision): major_(major), minor_(minor),
                                                 revision_(revision) {}
    Version(Version& other): major_(other.major_), minor_(other.minor_),
                             revision_(other.revision_) {}
    ~Version() {}
    V8_CONSTRUCTOR(Version, 3) {
        int arg0 = CheckInt(args[0]);
        int arg1 = CheckInt(args[1]);
        int arg2 = CheckInt(args[2]);
        
        instance = new Version(arg0, arg1, arg2);
    } V8_CONSTRUCTOR_END()
    
    int getMajor() const {return major_;}
    int getMinor() const {return minor_;}
    int getRevision() const {return revision_;}
    
    void setMajor(int major) {major_=major;}
    void setMinor(int minor) {minor_=minor;}
    void setRevision(int revision) {revision_=revision;}
    
    string toString() const {
        stringstream ret;
        ret << major_ << "." << minor_ << "." << revision_;
        return ret.str();
    }

    static V8_CALLBACK(ToString, 0) {
        Version& h = *(Unwrap<Version>(args.Holder()));
        return scope.Close(String::New(h.toString().c_str()));
    } V8_WRAP_END()
    
    //Getters
    static V8_GETTER(GetMajor) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        return scope.Close(Integer::New(h.major_));
    } V8_WRAP_END()
    static V8_GETTER(GetMinor) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        return scope.Close(Integer::New(h.minor_));
    } V8_WRAP_END()
    static V8_GETTER(GetRevision) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        return scope.Close(Integer::New(h.revision_));
    } V8_WRAP_END()
    
    //Setters
    static V8_SETTER(SetMajor) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        h.major_ = CheckInt(value);
    } V8_WRAP_END_NR()
    static V8_SETTER(SetMinor) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        h.minor_ = CheckInt(value);
    } V8_WRAP_END_NR()
    static V8_SETTER(SetRevision) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        h.revision_ = CheckInt(value);
    } V8_WRAP_END_NR()
private:
    int major_, minor_, revision_;
};

V8_CALLBACK(MarkdownVersion, 0) {
    int major, minor, revision;
    sd_version(&major, &minor, &revision);
    Version* ret = new Version(major, minor, revision);
    return scope.Close(ret->Wrapped());
} V8_WRAP_END()



////////////////////////////////////////////////////////////////////////////////
// MODULE DECLARATION
////////////////////////////////////////////////////////////////////////////////

#define RENDFUNC_V8_DEF(NAME, CPPFUNC)                                         \
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol(NAME),             \
            RendererWrap::CPPFUNC##_getter, RendererWrap::CPPFUNC##_setter);

void initVersion(Handle<Object> target) {
    HandleScope scope;
    
    Local<FunctionTemplate> protL = FunctionTemplate::New(&Version::NewInstance);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("Version"));

    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("major"), Version::GetMajor, Version::SetMajor);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("minor"), Version::GetMinor, Version::SetMinor);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("revision"), Version::GetRevision, Version::SetRevision);

    NODE_SET_PROTOTYPE_METHOD(prot, "toString", Version::ToString);
    
    target->Set(String::NewSymbol("Version"), prot->GetFunction());
    StoreTemplate("Version", prot);
}

void initRenderer(Handle<Object> target) {
    HandleScope scope;
    
    Local<FunctionTemplate> protL = FunctionTemplate::New(&RendererWrap::NewInstance);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("Renderer"));
    
    RENDFUNC_V8_DEF("hrule", hrule);
    
    target->Set(String::NewSymbol("Renderer"), prot->GetFunction());
    StoreTemplate("RendererWrap", prot);
}

void initHtmlRenderer(Handle<Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> protL = FunctionTemplate::New(&HtmlRendererWrap::NewInstance);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->Inherit(GetTemplate("RendererWrap"));
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("HtmlRenderer"));

    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("flags"), HtmlRendererWrap::GetFlags);

    target->Set(String::NewSymbol("HtmlRenderer"), prot->GetFunction());
    StoreTemplate("HtmlRendererWrap", prot);
}

void initMarkdown(Handle<Object> target) {
    HandleScope scope;

    Local<FunctionTemplate> protL = FunctionTemplate::New(&Markdown::NewInstance);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("Markdown"));

    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("extensions"), Markdown::GetExtensions);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("max_nesting"), Markdown::GetMaxNesting);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("renderer"), Markdown::GetRenderer);

    NODE_SET_PROTOTYPE_METHOD(prot, "renderSync", Markdown::RenderSync);
    
    target->Set(String::NewSymbol("Markdown"), prot->GetFunction());
    StoreTemplate("Markdown", prot);
}

extern "C" {
  void init(Handle<Object> target) {
    HandleScope scope;
    
    //VERSION class
    initVersion(target);

    //Markdown version function
    Local<FunctionTemplate> mv = FunctionTemplate::New(MarkdownVersion);
    target->Set(String::NewSymbol("markdownVersion"), mv->GetFunction());

    //Robotskirt version
    target->Set(String::NewSymbol("version"), (new Version(2,1,4))->Wrapped());

    //RENDERER class
    initRenderer(target);
    
    //HTMLRENDERER class
    initHtmlRenderer(target);
    
    //MARKDOWN class
    initMarkdown(target);
    
    //HTML TOC data
//    Local<FunctionTemplate> tocL = FunctionTemplate::New(TocData::New);
//    Persistent<FunctionTemplate> toc = Persistent<FunctionTemplate>::New(tocL);
//    toc->InstanceTemplate()->SetInternalFieldCount(1);
//    toc->SetClassName(String::NewSymbol("TocData"));
//
//    toc->InstanceTemplate()->SetAccessor(String::NewSymbol("headerCount"), TocData::GetHeaderCount, TocData::SetHeaderCount);
//    toc->InstanceTemplate()->SetAccessor(String::NewSymbol("currentLevel"), TocData::GetCurrentLevel, TocData::SetCurrentLevel);
//    toc->InstanceTemplate()->SetAccessor(String::NewSymbol("levelOffset"), TocData::GetLevelOffset, TocData::SetLevelOffset);
//
//    target->Set(String::NewSymbol("TocData"), toc->GetFunction());

    //Extension constants
    target->Set(String::NewSymbol("EXT_AUTOLINK"), Integer::New(MKDEXT_AUTOLINK));
    target->Set(String::NewSymbol("EXT_FENCED_CODE"), Integer::New(MKDEXT_FENCED_CODE));
    target->Set(String::NewSymbol("EXT_LAX_SPACING"), Integer::New(MKDEXT_LAX_SPACING));
    target->Set(String::NewSymbol("EXT_NO_INTRA_EMPHASIS"), Integer::New(MKDEXT_NO_INTRA_EMPHASIS));
    target->Set(String::NewSymbol("EXT_SPACE_HEADERS"), Integer::New(MKDEXT_SPACE_HEADERS));
    target->Set(String::NewSymbol("EXT_STRIKETHROUGH"), Integer::New(MKDEXT_STRIKETHROUGH));
    target->Set(String::NewSymbol("EXT_SUPERSCRIPT"), Integer::New(MKDEXT_SUPERSCRIPT));
    target->Set(String::NewSymbol("EXT_TABLES"), Integer::New(MKDEXT_TABLES));

    //Html renderer flags
    target->Set(String::NewSymbol("HTML_SKIP_HTML"), Integer::New(HTML_SKIP_HTML));
    target->Set(String::NewSymbol("HTML_SKIP_STYLE"), Integer::New(HTML_SKIP_STYLE));
    target->Set(String::NewSymbol("HTML_SKIP_IMAGES"), Integer::New(HTML_SKIP_IMAGES));
    target->Set(String::NewSymbol("HTML_SKIP_LINKS"), Integer::New(HTML_SKIP_LINKS));
    target->Set(String::NewSymbol("HTML_EXPAND_TABS"), Integer::New(HTML_EXPAND_TABS));
    target->Set(String::NewSymbol("HTML_SAFELINK"), Integer::New(HTML_SAFELINK));
    target->Set(String::NewSymbol("HTML_TOC"), Integer::New(HTML_TOC));
    target->Set(String::NewSymbol("HTML_HARD_WRAP"), Integer::New(HTML_HARD_WRAP));
    target->Set(String::NewSymbol("HTML_USE_XHTML"), Integer::New(HTML_USE_XHTML));
    target->Set(String::NewSymbol("HTML_ESCAPE"), Integer::New(HTML_ESCAPE));
  }
  NODE_MODULE(robotskirt, init)
}
}

