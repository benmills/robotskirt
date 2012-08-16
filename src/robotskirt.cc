#include "v8u.hpp"
#include <node_buffer.h>

#include <string>
#include <sstream>

extern "C" {
  #include"markdown.h"
  #include"html.h"
}

using namespace std;

using namespace node;
using namespace v8;
using namespace v8u;

namespace robotskirt {

// Constants taken from the official Sundown executable
#define OUTPUT_UNIT 64
#define DEFAULT_MAX_NESTING 16

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
    NG_SLOW_BUFFER,                                           \
    Integer::New(Buffer::Length(NG_SLOW_BUFFER))/*,           \
    Integer::New(0) <- WITH THIS WILL THROW AN ERROR*/        \
  };                                                          \
                                                              \
  NG_FAST_BUFFER = NG_JS_BUFFER->NewInstance(2, NG_JS_ARGS);

int64_t CheckInt(Handle<Value> value) {
  HandleScope scope;//FIXME: don't allow floating-point values
  if (!value->IsInt32()) V8_THROW(TypeErr("You must provide an integer!"));
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

// A C++ WRAPPER FOR Buf*, to ensure deallocation

class BufWrap {
public:
    BufWrap(buf* buf): buf_(buf) {}
    ~BufWrap() {
        bufrelease(buf_);
    }
    buf* get() {return buf_;}
    buf* operator->() {return buf_;}
    buf* operator*() {return buf_;}
private:
    buf* const buf_;
};

// CONVERTERS (especially buf* to Local<Object>)

Local<Object> toBuffer(const buf* buf) {
    HandleScope scope;
    Local<Object> ret;
    Handle<Object> buffer = Buffer::New(String::New((char*)buf->data, buf->size));
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
    V8_CL_CALLBACK(FunctionData, ToString, 0) {
        return scope.Close(Str("<Native function>"));
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
    Local<FunctionTemplate> repr = Func(FunctionData::ToString);
    wrap->Set(Symbol("toString"), repr);
    wrap->Set(Symbol("inspect"), repr);
    wrap->SetCallAsFunctionHandler(wrapper);
    SetPersistent(handle, wrap->NewInstance());
    (new FunctionData(func,sig,opaque))->Wrap(handle);
}

//What to do if no function set?
#define NULL_ACTION()                                                          \
    V8_THROW(TypeErr("No function was set for this action."));

#define NULL_ACTION_R() NULL_ACTION()

// WRAPPERS (call a [wrapped] CPP function from JS)

#define WRAPPER_CALL_void()
#define WRAPPER_CALL_int() int v =

#define WRAPPER_POST_CALL_void()
#define WRAPPER_POST_CALL_int()                                                \
    if (!v) return scope.Close(False());

#define BUF1_WRAPPER(CPPFUNC, RET)                                             \
    static V8_CALLBACK(CPPFUNC##_wrapper, 0) {                                 \
        V8_UNWRAP(FunctionData, args)                                          \
                                                                               \
        BufWrap ob (bufnew(OUTPUT_UNIT));                                      \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, void*))inst->getFunction())        \
                (*ob,  inst->getOpaque());                                     \
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
            putToBuf(ob, Obj(ret));                                            \
            return BINDER_RETURN_##RET;                                        \
        }                                                                      \
        if (ret->IsString()) {                                                 \
            putToBuf(ob, Buffer::New(Local<String>::Cast(ret)));               \
            return BINDER_RETURN_##RET;                                        \
        }                                                                      \
        BINDER_PRE_ERROR_##RET()                                               \
        V8_THROW(TypeErr("You should return a Buffer or a String"));           \
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
        V8_UNWRAP(RendererWrap, info)                                          \
        if (inst->CPPFUNC.IsEmpty()) return scope.Close(Undefined());          \
        return scope.Close(inst->CPPFUNC);                                     \
    } V8_WRAP_END()

#define _RENDFUNC_SETTER(CPPFUNC, SIGNATURE)                                   \
    static V8_SETTER(CPPFUNC##_setter) {                                       \
        V8_UNWRAP(RendererWrap, info)                                          \
        inst->rend.CPPFUNC = &CPPFUNC##_binder;                                \
        if (!Bool(value)) {                                                    \
            ClearPersistent<Object>(inst->CPPFUNC);                            \
            return;                                                            \
        }                                                                      \
        if (!value->IsObject()) V8_THROW(TypeErr("Value must be a function!"));\
        Local<Object> obj = Obj(value);                                        \
        if (!obj->IsCallable()) V8_THROW(TypeErr("Value must be a function!"));\
                                                                               \
        if (setCppFunction(&(inst->CPPFUNC##_orig),                            \
                           &(inst->CPPFUNC##_opaque),                          \
                           obj, SIGNATURE))                                    \
            inst->rend.CPPFUNC = &CPPFUNC##_forwarder;                         \
        SetPersistent(inst->CPPFUNC, obj);                                     \
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
    V8_CL_WRAPPER("robotskirt::RendererWrap")
    RendererWrap() {}
    ~RendererWrap() {
        ClearPersistent<Object>(hrule);
    }
    V8_CL_CTOR(RendererWrap, 0) {
        inst = new RendererWrap();
    } V8_CL_CTOR_END()
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
    V8_CL_WRAPPER("robotskirt::HtmlRendererWrap")
    HtmlRendererWrap(unsigned int flags): flags_(flags), options() {//TODO: custom options
        sdhtml_renderer(&rend, &options, 0);
        wrap_functions(&options);
    }
    ~HtmlRendererWrap() {}
    V8_CL_CTOR(HtmlRendererWrap, 0) {
        //Extract arguments
        unsigned int flags = 0;
        if (args.Length() >= 1) {
            flags = CheckFlags(args[0]);
        }

        inst = new HtmlRendererWrap(flags);
    } V8_CL_CTOR_END()

    V8_CL_GETTER(HtmlRendererWrap, Flags) {
        return scope.Close(Uint(inst->flags_));
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
    V8_CL_WRAPPER("robotskirt::Markdown")
    Markdown(RendererWrap* renderer, unsigned int extensions, size_t max_nesting):
            markdown(sd_markdown_new(extensions, max_nesting, &renderer->rend, renderer)),
            renderer_(renderer), max_nesting_(max_nesting), extensions_(extensions) {
        SetPersistent<Object>(renderer_handle, renderer->handle_);
    }
    ~Markdown() {
        ClearPersistent<Object>(renderer_handle);
        sd_markdown_free(markdown);
    }
    V8_CL_CTOR(Markdown, 1) {
        //Check & extract arguments
        if (!args[0]->IsObject()) V8_THROW(TypeErr("You must provide a Renderer!"));
        Local<Object> obj = Obj(args[0]);

        if (!GetTemplate("robotskirt::RendererWrap")->HasInstance(obj))
            V8_THROW(TypeErr("You must provide a Renderer!"));
        RendererWrap* rend = Unwrap<RendererWrap>(obj);

        unsigned int flags = 0;
        size_t max_nesting = DEFAULT_MAX_NESTING;
        if (args.Length()>=2) {
            flags = CheckFlags(args[1]);
            if (args.Length()>=3) {
                max_nesting = CheckInt(args[2]);
            }
        }

        inst = new Markdown(rend, flags, max_nesting);
    } V8_CL_CTOR_END()

    V8_CL_GETTER(Markdown, Renderer) {
        return scope.Close(inst->renderer_handle);
    } V8_WRAP_END()
    V8_CL_GETTER(Markdown, MaxNesting) {
        return scope.Close(Uint(inst->max_nesting_));
    } V8_WRAP_END()
    V8_CL_GETTER(Markdown, Extensions) {
        return scope.Close(Uint(inst->extensions_));
    } V8_WRAP_END()
    
    //And the most important function(s)...
    V8_CL_CALLBACK(Markdown, RenderSync, 1) {
        //Extract input
        Local<Value> arg = args[0];
        Handle<Object> obj;
        if (Buffer::HasInstance(arg)) obj = Obj(arg);
        else if (arg->IsString()) obj = Buffer::New(Local<String>::Cast(arg));
        else V8_THROW(TypeErr("You must provide a Buffer or a String!"));
        char* data = Buffer::Data(obj);
        size_t length = Buffer::Length(obj);
        
        //Prepare
        BufWrap out (bufnew(OUTPUT_UNIT));
        
        //GO!!
        sd_markdown_render(*out, (uint8_t*)data, length, inst->markdown);
        
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
    V8_CL_WRAPPER("robotskirt::Version")
    Version(int major, int minor, int revision): major_(major), minor_(minor),
                                                 revision_(revision) {}
    Version(Version& other): major_(other.major_), minor_(other.minor_),
                             revision_(other.revision_) {}
    ~Version() {}
    V8_CL_CTOR(Version, 3) {
        int arg0 = CheckInt(args[0]);
        int arg1 = CheckInt(args[1]);
        int arg2 = CheckInt(args[2]);
        
        inst = new Version(arg0, arg1, arg2);
    } V8_CL_CTOR_END()
    
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

    V8_CL_CALLBACK(Version, ToString, 0) {
        return scope.Close(Str(inst->toString()));
    } V8_WRAP_END()
    
    V8_CL_CALLBACK(Version, Inspect, 0) {
        return scope.Close(Str("<Version "+inst->toString()+">"));
    } V8_WRAP_END()
    
    //Getters
    V8_CL_GETTER(Version, Major) {
        return scope.Close(Int(inst->major_));
    } V8_WRAP_END()
    V8_CL_GETTER(Version, Minor) {
        return scope.Close(Int(inst->minor_));
    } V8_WRAP_END()
    V8_CL_GETTER(Version, Revision) {
        return scope.Close(Int(inst->revision_));
    } V8_WRAP_END()
    
    //Setters
    V8_CL_SETTER(Version, Major) {
        inst->major_ = CheckInt(value);
    } V8_WRAP_END_NR()
   V8_CL_SETTER(Version, Minor) {
        inst->minor_ = CheckInt(value);
    } V8_WRAP_END_NR()
    V8_CL_SETTER(Version, Revision) {
        inst->revision_ = CheckInt(value);
    } V8_WRAP_END_NR()
private:
    int major_, minor_, revision_;
};

Local<Object> SundownVersion() {
    int major, minor, revision;
    sd_version(&major, &minor, &revision);
    Version* ret = new Version(major, minor, revision);
    return ret->Wrapped();
}



////////////////////////////////////////////////////////////////////////////////
// MODULE DECLARATION
////////////////////////////////////////////////////////////////////////////////

#define RENDFUNC_V8_DEF(NAME, CPPFUNC)                                         \
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol(NAME),             \
            RendererWrap::CPPFUNC##_getter, RendererWrap::CPPFUNC##_setter);

NODE_DEF_TYPE(Version, "Version") {
    V8_DEF_PROP(Version, Major, "major");
    V8_DEF_PROP(Version, Minor, "minor");
    V8_DEF_PROP(Version, Revision, "revision");

    V8_DEF_METHOD(Version, ToString, "toString");
    V8_DEF_METHOD(Version, Inspect, "inspect");

    StoreTemplate("robotskirt::Version", prot);
} NODE_DEF_TYPE_END()

NODE_DEF_TYPE(RendererWrap, "Renderer") {
    RENDFUNC_V8_DEF("hrule", hrule);

    StoreTemplate("robotskirt::RendererWrap", prot);
} NODE_DEF_TYPE_END()

NODE_DEF_TYPE(HtmlRendererWrap, "HtmlRenderer") {
    V8_INHERIT("robotskirt::RendererWrap");

    V8_DEF_RPROP(HtmlRendererWrap, Flags, "flags");

    StoreTemplate("robotskirt::HtmlRendererWrap", prot);
} NODE_DEF_TYPE_END()

NODE_DEF_TYPE(Markdown, "Markdown") {
    V8_DEF_RPROP(Markdown, Extensions, "extensions");
    V8_DEF_RPROP(Markdown, MaxNesting, "maxNesting");
    V8_DEF_RPROP(Markdown, Renderer, "renderer");

    V8_DEF_METHOD(Markdown, RenderSync, "renderSync");

    StoreTemplate("robotskirt::Markdown", prot);
} NODE_DEF_TYPE_END()

NODE_DEF_MAIN() {
    //Initialize classes
    initVersion(target);
    initRendererWrap(target);
    initHtmlRendererWrap(target);
    initMarkdown(target);

    //Versions hash
    Local<Object> versions = Obj();
    versions->Set(Symbol("sundown"), SundownVersion());
    versions->Set(Symbol("robotskirt"), (new Version(2,2,0))->Wrapped());
    target->Set(Symbol("versions"), versions);

    //Extension constants
    target->Set(Symbol("EXT_AUTOLINK"), Int(MKDEXT_AUTOLINK));
    target->Set(Symbol("EXT_FENCED_CODE"), Int(MKDEXT_FENCED_CODE));
    target->Set(Symbol("EXT_LAX_SPACING"), Int(MKDEXT_LAX_SPACING));
    target->Set(Symbol("EXT_NO_INTRA_EMPHASIS"), Int(MKDEXT_NO_INTRA_EMPHASIS));
    target->Set(Symbol("EXT_SPACE_HEADERS"), Int(MKDEXT_SPACE_HEADERS));
    target->Set(Symbol("EXT_STRIKETHROUGH"), Int(MKDEXT_STRIKETHROUGH));
    target->Set(Symbol("EXT_SUPERSCRIPT"), Int(MKDEXT_SUPERSCRIPT));
    target->Set(Symbol("EXT_TABLES"), Int(MKDEXT_TABLES));

    //Html renderer flags
    target->Set(Symbol("HTML_SKIP_HTML"), Int(HTML_SKIP_HTML));
    target->Set(Symbol("HTML_SKIP_STYLE"), Int(HTML_SKIP_STYLE));
    target->Set(Symbol("HTML_SKIP_IMAGES"), Int(HTML_SKIP_IMAGES));
    target->Set(Symbol("HTML_SKIP_LINKS"), Int(HTML_SKIP_LINKS));
    target->Set(Symbol("HTML_EXPAND_TABS"), Int(HTML_EXPAND_TABS));
    target->Set(Symbol("HTML_SAFELINK"), Int(HTML_SAFELINK));
    target->Set(Symbol("HTML_TOC"), Int(HTML_TOC));
    target->Set(Symbol("HTML_HARD_WRAP"), Int(HTML_HARD_WRAP));
    target->Set(Symbol("HTML_USE_XHTML"), Int(HTML_USE_XHTML));
    target->Set(Symbol("HTML_ESCAPE"), Int(HTML_ESCAPE));
} NODE_DEF_MAIN_END(robotskirt)

}

