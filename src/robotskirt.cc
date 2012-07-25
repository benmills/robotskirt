#include <v8.h>
#include <node.h>
#include <node_buffer.h>

#include <string>
#include <sstream>

#include "markdownWrapper.hpp"

using namespace std;

using namespace node;
using namespace v8;

using namespace mkd;

// Constants taken from the official executable
#define OUTPUT_UNIT 64
#define DEFAULT_MAX_NESTING 16

namespace robotskirt {

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
  Handle<Value> NG_JS_ARGS[1] = {                             \
    Local<Value>::New(NG_SLOW_BUFFER->handle_),                                  \
    /*Integer::New(Buffer::Length(NG_SLOW_BUFFER)),             \
    Integer::New(0)    */                                       \
  };                                                          \
                                                              \
  NG_FAST_BUFFER = NG_JS_BUFFER->NewInstance(1, NG_JS_ARGS);

// V8 exception wrapping

Persistent<Value> type_error(const char* message) {
    throw Persistent<Value>::New(Exception::TypeError(String::New(message)));
}

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
// RENDER FUNCTIONS STUFF
////////////////////////////////////////////////////////////////////////////////

// SIGNATURES
enum CppSignature {
    BUF1,
    BUF2,
    BUF2INT,
    BUF3,
    INT_BUF2,
    INT_BUF2INT,
    INT_BUF4,
    INT_BUF1
};

// CONVERTERS (especially buf* to Local<Object>

Local<Object> toBuffer(const buf* buf) {
    HandleScope scope;
    //FIXME: this doesn't seem to work on new versions of Node
//    Local<Object> ret;
//    Buffer* buffer = Buffer::New((char*)buf->data, buf->size); //should we use asize instead?
//    MAKE_FAST_BUFFER(buffer, ret);
//    return scope.Close(ret);
    Buffer* buffer = Buffer::New((char*)buf->data, buf->size);
    return scope.Close(Local<Object>::New(buffer->handle_));
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
    wrap->SetCallAsFunctionHandler(wrapper);
    SetPersistent<Object>(handle, wrap->NewInstance());
    (new FunctionData(func,sig,opaque))->Wrap(handle);
}

//What to do if no function set?
#define NULL_ACTION()                                                          \
    type_error("No function was set for this action.");

#define NULL_ACTION_R() NULL_ACTION()

// WRAPPERS (call a CPP function from JS)

//TODO

// BINDERS (call a JS function from CPP)

#define BINDER_BUF1(CPPFUNC)                                                   \
    void CPPFUNC##_binder(struct buf *ob, void *opaque) {                      \
                                                                               \
    }

//TODO

////////////////////////////////////////////////////////////////////////////////
// RENDERER CLASS DECLARATION
////////////////////////////////////////////////////////////////////////////////

// JS ACCESSORS

#define RENDFUNC_GETTER(CPPFUNC)                                               \
    static V8_GETTER(CPPFUNC##_getter) {                                       \
        RendererWrap* inst = Unwrap<RendererWrap>(info.Holder());              \
        Persistent<Object> func = inst->CPPFUNC;                               \
        if (func.IsEmpty()) return scope.Close(Undefined());                   \
        return scope.Close(func);                                              \
    } V8_WRAP_END()

#define RENDFUNC_VAR(CPPFUNC)                                                  \
    Persistent<Object> CPPFUNC;                                                \
    void* CPPFUNC##_orig;                                                      \
    void* CPPFUNC##_opaque;

// The three macros together
#define RENDFUNC_DEF(CPPFUNC, SIGNATURE)                                       \
    protected: RENDFUNC_VAR(CPPFUNC)                                           \
    public:                                                                    \
        RENDFUNC_GETTER(CPPFUNC)

// Forward declaration, to make things work
class Markdown;

class RendererWrap: public ObjectWrap {
public:
    RendererWrap() {}
    ~RendererWrap() {
        ClearPersistent<Object>(hrule);
    }
    static V8_CALLBACK(NewInstance, 0) {
        (new RendererWrap())->Wrap(args.This());
        return scope.Close(args.This());
    } V8_WRAP_END()
    static V8_SETTER(hrule_setter) {
        RendererWrap* inst = Unwrap<RendererWrap>(info.Holder());
        inst->rend.hrule = &hrule_binder;
        if (!value->BooleanValue()) {
            ClearPersistent<Object>(inst->hrule);
            return;
        }
        if (!value->IsObject()) type_error("Value must be a function!");
        Local<Object> obj = value->ToObject();
        if (!obj->IsCallable()) type_error("Value must be a function!");
        
        if (setCppFunction(&(inst->hrule_orig), &(inst->hrule_opaque), obj, BUF1))
            inst->rend.hrule = &hrule_forwarder;
        SetPersistent<Object>(inst->hrule, obj);
    } V8_WRAP_END_NR()
    static void hrule_binder(struct buf *ob, void *opaque) {
        HandleScope scope;
        
        Persistent<Object>& obj = ((RendererWrap*)opaque)->hrule;
        if (obj.IsEmpty()) {
            NULL_ACTION()
        }
        
        //Convert arguments
        Local<Value> args [0];

        //Call it!
        TryCatch trycatch;
        Local<Value> ret = obj->CallAsFunction(Context::GetCurrent()->Global(), 0, args);
        if (trycatch.HasCaught())
            throw Persistent<Value>::New(trycatch.Exception());
        //Convert the result back
        if (!Buffer::HasInstance(ret)) type_error("You should return a buffer!");
        putToBuf(ob, Handle<Object>::Cast(ret));
    }
    static void hrule_forwarder(struct buf *ob, void *opaque) {
        RendererWrap* rend = (RendererWrap*)opaque;
        ((void(*)(struct buf *ob, void *opaque))rend->hrule_orig)(ob,  rend->hrule_opaque);
    }
    static V8_CALLBACK(hrule_wrapper, 0) {
        FunctionData *data = Unwrap<FunctionData>(args.Holder());
        void (*func)(struct buf *ob, void *opaque) = (void(*)(buf*,void*))(data->getFunction());
        
        buf* ob = bufnew(OUTPUT_UNIT);
        func(ob, data->getOpaque());
        Local<Object> ret = toBuffer(ob);
        ob->data = NULL;
        bufrelease(ob);
        return scope.Close(ret);
    } V8_WRAP_END()
protected:
    sd_callbacks rend;

friend class Markdown;    
// Renderer functions
RENDFUNC_DEF(hrule, BUF1)
};

class HtmlRendererWrap: public RendererWrap {
public: //FIXME: fix "constructors called as functions" bug
    HtmlRendererWrap(unsigned int flags): flags_(flags), options() {//TODO: custom options & getters for flags also
        sdhtml_renderer(&rend, &options, 0);
        
        hrule_orig = (void*)rend.hrule;
        hrule_opaque = &options;
        rend.hrule = &hrule_forwarder;
        jsFunction(hrule, hrule_orig, BUF1, &hrule_wrapper, &options); //FIXME: put in outer method
    }
    ~HtmlRendererWrap() {}
    static V8_CALLBACK(NewInstance, 0) {
        //Extract arguments
        unsigned int flags = 0;
        if (args.Length() >= 1) {
            flags = CheckFlags(args[0]);
        }
        
        (new HtmlRendererWrap(flags))->Wrap(args.This());
        return scope.Close(args.This());
    } V8_WRAP_END()
    static V8_GETTER(GetFlags) {
        HtmlRendererWrap* inst = Unwrap<HtmlRendererWrap>(info.Holder());
        return scope.Close(Integer::New(inst->flags_));
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
    Markdown(RendererWrap* renderer, unsigned int extensions, size_t max_nesting):
            markdown(sd_markdown_new(extensions, max_nesting, &renderer->rend, renderer)),
            renderer_(renderer), max_nesting_(max_nesting), extensions_(extensions) {
        SetPersistent<Object>(renderer_handle, renderer->handle_);
    }
    ~Markdown() {
        ClearPersistent<Object>(renderer_handle);
        sd_markdown_free(markdown);
    }
    static V8_CALLBACK(NewInstance, 1) {
        //Extract arguments
        if (!args[0]->IsObject()) type_error("You must provide a Renderer!");
        Local<Object> obj = args[0]->ToObject();
        //FIXME: maybe there's a better way to do that?
        if (!((FunctionTemplate*)External::Cast(*args.Data())->Value())->HasInstance(obj))
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
        
        //Construct / wrap the object
        (new Markdown(rend, flags, max_nesting))->Wrap(args.This());
        return scope.Close(args.This());
    } V8_WRAP_END()
    
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
        //FIXME
        if (!Buffer::HasInstance(args[0]))
            type_error("You MUST provide a BUFFER!");
        Local<Object> obj = Local<Object>::Cast(args[0]);
        char* data; size_t length;
        data = Buffer::Data(obj);
        length = Buffer::Length(obj);
        
        //Prepare
        buf* out = bufnew(OUTPUT_UNIT); //XXX TODO: ensure deallocation
        
        //GO!!
        sd_markdown_render(out, (uint8_t*)data, length, md->markdown);
        
        //Finish and...
        Buffer* buffer = Buffer::New((char*)out->data, out->size);
        Local<Object> fastBuffer;
        MAKE_FAST_BUFFER(buffer, fastBuffer);
        out->data = NULL;
        //...cleanup
        bufrelease(out);
        
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
private:
    int major_, minor_, revision_;
public:
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
    
    Version(int major, int minor, int revision): major_(major), minor_(minor),
                                                 revision_(revision) {}
    Version(Version& other): major_(other.major_), minor_(other.minor_),
                             revision_(other.revision_) {}
    ~Version() {}
    
    static V8_CALLBACK(NewVersion, 3) {
        int arg0 = CheckInt(args[0]);
        int arg1 = CheckInt(args[1]);
        int arg2 = CheckInt(args[2]);
        
        (new Version(arg0, arg1, arg2))->Wrap(args.This());
        return scope.Close(args.This());
    } V8_WRAP_END()
    
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
};

Handle<Value> newVersionInstance(Local<Value> data, int major, int minor, int revision) {
    Local<Function> func = Local<Function>::Cast(data);
    Handle<Value> argv [3] = {Integer::New(major), Integer::New(minor), Integer::New(revision)};
    return func->NewInstance(3, argv);
}

V8_CALLBACK(MarkdownVersion, 0) {
    int major, minor, revision;
    sd_version(&major, &minor, &revision);
    return scope.Close(newVersionInstance(args.Data(), major, minor, revision));
} V8_WRAP_END()



////////////////////////////////////////////////////////////////////////////////
// MODULE DECLARATION
////////////////////////////////////////////////////////////////////////////////

Persistent<FunctionTemplate> initRenderer(Handle<Object> target) {
    HandleScope scope;
    
    Local<FunctionTemplate> protL = FunctionTemplate::New(&RendererWrap::NewInstance);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("Renderer"));
    
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("hrule"), RendererWrap::hrule_getter, RendererWrap::hrule_setter);
    
    target->Set(String::NewSymbol("Renderer"), prot->GetFunction());
    return prot;
}

Persistent<FunctionTemplate> initHtmlRenderer(Handle<Object> target, Handle<FunctionTemplate> rend) {
    HandleScope scope;
    
    Local<FunctionTemplate> protL = FunctionTemplate::New(&HtmlRendererWrap::NewInstance);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->Inherit(rend);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("HtmlRenderer"));

    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("flags"), HtmlRendererWrap::GetFlags);

    target->Set(String::NewSymbol("HtmlRenderer"), prot->GetFunction());
    return prot;
}

Persistent<FunctionTemplate> initMarkdown(Handle<Object> target, Persistent<FunctionTemplate> rend) {
    HandleScope scope;
    
    Local<FunctionTemplate> protL = FunctionTemplate::New(&Markdown::NewInstance, External::New(*rend));
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(protL);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("Markdown"));
    
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("extensions"), Markdown::GetExtensions);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("max_nesting"), Markdown::GetMaxNesting);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("renderer"), Markdown::GetRenderer);
    
    NODE_SET_PROTOTYPE_METHOD(prot, "renderSync", Markdown::RenderSync);
    
    target->Set(String::NewSymbol("Markdown"), prot->GetFunction());
    return prot;
}

extern "C" {
  void init(Handle<Object> target) {
    HandleScope scope;

    //Markdown version class
    Local<FunctionTemplate> verL = FunctionTemplate::New(&Version::NewVersion);
    Persistent<FunctionTemplate> ver = Persistent<FunctionTemplate>::New(verL);
    ver->InstanceTemplate()->SetInternalFieldCount(1);
    ver->SetClassName(String::NewSymbol("Version"));

    ver->InstanceTemplate()->SetAccessor(String::NewSymbol("major"), Version::GetMajor, Version::SetMajor);
    ver->InstanceTemplate()->SetAccessor(String::NewSymbol("minor"), Version::GetMinor, Version::SetMinor);
    ver->InstanceTemplate()->SetAccessor(String::NewSymbol("revision"), Version::GetRevision, Version::SetRevision);

    NODE_SET_PROTOTYPE_METHOD(ver, "toString", Version::ToString);
    target->Set(String::NewSymbol("Version"), ver->GetFunction());

    //Markdown version function
    Local<FunctionTemplate> mv = FunctionTemplate::New(MarkdownVersion, ver->GetFunction());
    target->Set(String::NewSymbol("markdownVersion"), mv->GetFunction());

    //Robotskirt version
    target->Set(String::NewSymbol("version"), newVersionInstance(ver->GetFunction(), 2,1,1 ));

    //RENDERER class
    Persistent<FunctionTemplate> rend = initRenderer(target);
    
    //HTMLRENDERER class
    Persistent<FunctionTemplate> htmlrend = initHtmlRenderer(target, rend);
    
    //MARKDOWN class
    Persistent<FunctionTemplate> markdown = initMarkdown(target, rend);
    
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

