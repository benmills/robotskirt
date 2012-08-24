#include "v8u.hpp"
#include "version.hpp"
#include <node_buffer.h>

#include <string>

extern "C" {
  #include"markdown.h"
  #include"html.h"
}

using namespace std;

using namespace node;
using namespace v8;
using namespace v8u;
//using namespace v8u::Version

namespace robotskirt {

// Constants taken from the official Sundown executable 
#define OUTPUT_UNIT 64
#define DEFAULT_MAX_NESTING 16

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
  Handle<Value> NG_JS_ARGS[2] = {                             \
    NG_SLOW_BUFFER,                                           \
    Integer::New(Buffer::Length(NG_SLOW_BUFFER))/*,           \
    Integer::New(0) <- WITH THIS WILL THROW AN ERROR*/        \
  };                                                          \
                                                              \
  NG_FAST_BUFFER = NG_JS_BUFFER->NewInstance(2, NG_JS_ARGS);

//DEPRECATED: Use Int() or Uint()
inline int64_t CheckInt(Handle<Value> value) {
  if (!value->IsInt32()) V8_THROW(TypeErr("You must provide an integer!"));
  return value->IntegerValue();
}

int CheckFlags(Handle<Value> hdl) {
  HandleScope scope;
  if (hdl->IsArray()) {
    int ret = 0;
    Handle<Array> array = Handle<Array>::Cast(hdl);
    for (uint32_t i=0; i<array->Length(); i++)
      ret |= Int(array->Get(i));
    return ret;
  }
  return Int(hdl);
}

unsigned int CheckUFlags(Handle<Value> hdl) {
  HandleScope scope;
  if (hdl->IsArray()) {
    unsigned int ret = 0;
    Handle<Array> array = Handle<Array>::Cast(hdl);
    for (uint32_t i=0; i<array->Length(); i++)
      ret |= Uint(array->Get(i));
    return ret;
  }
  return Uint(hdl);
}

//A reference counter
class RendFuncData {
public:
  RendFuncData() : refs(0), unrefs(0) {}
  void unref() {
    unrefs++;
    if (unrefs > refs) delete this;
  }
  void ref() {
    refs++;
  }
  virtual void* ptr() = 0;
  virtual ~RendFuncData() {};
private:
  unsigned char refs;
  unsigned char unrefs;
};
class HtmlRendFuncData : public RendFuncData {
public:
  HtmlRendFuncData() : opt(new html_renderopt) {}
  ~HtmlRendFuncData() {delete opt;}
  void* ptr() {return opt;};
private:
  html_renderopt* const opt;
};

////////////////////////////////////////////////////////////////////////////////
// RENDER FUNCTIONS STUFF
////////////////////////////////////////////////////////////////////////////////

// SIGNATURES
enum CppSignature {
    void_BUF1,
    void_BUF2,
    void_BUF2INT,
    void_BUF3,
     int_BUF1,
     int_BUF2,
     int_BUF2INT,
     int_BUF4
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

//DEPRECATED: use toString instead
Local<Object> toBuffer(const buf* buf) {
    HandleScope scope;
    Local<Object> ret;
    Handle<Object> buffer = Buffer::New(String::New((char*)buf->data, buf->size));
    MAKE_FAST_BUFFER(buffer, ret);
    return scope.Close(ret);
}
//DEPRECATED: unsafe, use makeBuf instead
void setToBuf(buf* target, Handle<Object> obj) {
    bufreset(target);
    target->data = (uint8_t*)Buffer::Data(obj);
    target->asize = target->size = Buffer::Length(obj);
    Buffer::Initialize(obj);
}
inline void putToBuf(buf* target, Handle<Value> obj) {
    String::Utf8Value str (obj);
    bufput(target, *str, str.length());
}
inline Local<String> toString(const buf* buf) {
    return String::New(reinterpret_cast<const char*>(buf->data), buf->size);
}
inline void makeBuf(buf& target, String::Utf8Value& text) {
  target.data = (uint8_t*)(*text);
  target.size = target.asize = text.length();
  target.unit = 0;
}

// FUNCTION DATA (this gets injected into CPP functions converted to JS)
class FunctionData;
typedef v8::Handle<v8::Value> (*PassInvocationCallback)(robotskirt::FunctionData*, const v8::Arguments&);
class FunctionData: public ObjectWrap {
public:
    V8_CL_WRAPPER("robotskirt::FunctionData")
    static Handle<Value> NewInstance(const v8::Arguments& args) {
        v8::HandleScope scope;
        if ((args.Length()==1) && (args[0]->IsExternal())) {
            ((FunctionData*)External::Unwrap(args[0]))->Wrap(args.This());
            return scope.Close(args.This());
        }
        return ThrowException(Err("You can't instantaniate this from the JS side."));
    }
    FunctionData(void* function, CppSignature signature, RendFuncData* opaque, PassInvocationCallback wrapper):
            function_(function), signature_(signature), opaque_(opaque), wrapper_(wrapper) {
        opaque->ref();
    }
    ~FunctionData() {
        opaque_->unref();
    }
    void* getFunction() {return function_;}
    void* getOpaque() {return opaque_->ptr();}
    CppSignature getSignature() {return signature_;}
    static V8_CALLBACK(ToString, 0) {
        return scope.Close(Str("<Native function>"));
    } V8_WRAP_END()
    static Handle<Value> Call(const Arguments& args) {
        HandleScope scope;
        V8_UNWRAP(FunctionData, args)
        return inst->wrapper_(inst, args);
    }
protected:
    void* const function_;
    CppSignature const signature_;
    RendFuncData* const opaque_;
    PassInvocationCallback wrapper_;
};

// CONVERT BETWEEN CPP AND JS FUNCTIONS

bool setCppFunction(void** func, void** opaque, Handle<Object> obj, CppSignature sig) {
    HandleScope scope;
    if (!GetTemplate("robotskirt::FunctionData")->HasInstance(obj)) return false;
    FunctionData* data = ObjectWrap::Unwrap<FunctionData>(obj);
    if (sig != data->getSignature()) return false;
    *func = data->getFunction();
    *opaque = data->getOpaque();
    return true;
}

Local<Object> jsFunction(void* func, CppSignature sig, PassInvocationCallback wrapper, RendFuncData* opaque) {
    return (new FunctionData(func,sig,opaque,wrapper))->Wrapped();
}

// WRAPPERS (call a [wrapped] CPP function from JS)

#define W_OUTPUT_UNIT OUTPUT_UNIT

#define WRAPPER_CALL_void()
#define WRAPPER_CALL_int() int v =

#define WRAPPER_POST_CALL_void()
#define WRAPPER_POST_CALL_int()                                                \
    if (!v) return False();

#define WRAPPERS(SIGBASE)  SIGBASE##_WRAPPER(void) SIGBASE##_WRAPPER(int)

// For the wrappers we don't use V8U wrapping,
// as we don't need any of his features.

#define BUF1_WRAPPER(RET)                                                      \
    Handle<Value> BUF1_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, void*))inst->getFunction())        \
                (*ob,  inst->getOpaque());                                     \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF1)

#define BUF2_WRAPPER(RET)                                                      \
    Handle<Value> BUF2_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        CheckArguments(1,args);                                                \
        String::Utf8Value texts (args[0]);                                     \
        buf text;                                                              \
        makeBuf(text, texts);                                                  \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, void*))inst->getFunction())\
                (*ob, &text,  inst->getOpaque());                              \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF2)

#define BUF2INT_WRAPPER(RET)                                                   \
    Handle<Value> BUF2INT_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        CheckArguments(2,args);                                                \
        String::Utf8Value texts (args[0]);                                     \
        buf text;                                                              \
        makeBuf(text, texts);                                                  \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, int, void*))inst->getFunction())\
                (*ob, &text, Int(args[1]),  inst->getOpaque());                \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF2INT)

#define BUF3_WRAPPER(RET)                                                      \
    Handle<Value> BUF3_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        CheckArguments(2,args);                                                \
        String::Utf8Value texts (args[0]);                                     \
        buf text;                                                              \
        makeBuf(text, texts);                                                  \
                                                                               \
        String::Utf8Value langs (args[1]);                                     \
        buf lang;                                                              \
        makeBuf(lang, langs);                                                  \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, const buf*, void*))inst->getFunction())\
                (*ob, &text, &lang,  inst->getOpaque());                       \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF3)

#define BUF4_WRAPPER(RET)                                                      \
    Handle<Value> BUF4_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        CheckArguments(3,args);                                                \
        String::Utf8Value links (args[0]);                                     \
        buf link;                                                              \
        makeBuf(link, links);                                                  \
                                                                               \
        String::Utf8Value titles (args[1]);                                    \
        buf title;                                                             \
        makeBuf(title, titles);                                                \
                                                                               \
        String::Utf8Value conts (args[2]);                                     \
        buf cont;                                                              \
        makeBuf(cont, conts);                                                  \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, const buf*, const buf*, void*))inst->getFunction())\
                (*ob, &link, &title, &cont,  inst->getOpaque());               \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF4)

// BINDERS (call a JS function from CPP)

#define BINDER_RETURN_void
#define BINDER_RETURN_int  1

#define BINDER_RETURN_NULL_void
#define BINDER_RETURN_NULL_int  1

#define BUF1_BINDER(CPPFUNC, RET)                                              \
    static RET CPPFUNC##_binder(struct buf *ob, void *opaque) {                \
        HandleScope scope;                                                     \
                                                                               \
        /*Convert arguments*/                                                  \
        Local<Value> args [0];                                                 \
                                                                               \
        /*Call it!*/                                                           \
        TryCatch trycatch;                                                     \
        Local<Value> ret = ((RendererData*)opaque)->CPPFUNC->CallAsFunction(Context::GetCurrent()->Global(), 0, args);\
        if (trycatch.HasCaught())                                              \
            V8_THROW(trycatch.Exception());                                    \
        /*Convert the result back*/                                            \
        if (ret->IsFalse()) return BINDER_RETURN_NULL_##RET;                   \
        putToBuf(ob, ret);                                                     \
        return BINDER_RETURN_##RET;                                            \
    }

#define BUF2_BINDER(CPPFUNC, RET)                                              \
    static RET CPPFUNC##_binder(struct buf *ob, const struct buf *text, void *opaque) {\
        HandleScope scope;                                                     \
                                                                               \
        /*Convert arguments*/                                                  \
        Local<Value> args [1] = {toString(text)};                              \
                                                                               \
        /*Call it!*/                                                           \
        TryCatch trycatch;                                                     \
        Local<Value> ret = ((RendererData*)opaque)->CPPFUNC->CallAsFunction(Context::GetCurrent()->Global(), 1, args);\
        if (trycatch.HasCaught())                                              \
            V8_THROW(trycatch.Exception());                                    \
        /*Convert the result back*/                                            \
        if (ret->IsFalse()) return BINDER_RETURN_NULL_##RET;                   \
        putToBuf(ob, ret);                                                     \
        return BINDER_RETURN_##RET;                                            \
    }

#define BUF2INT_BINDER(CPPFUNC, RET)                                           \
    static RET CPPFUNC##_binder(struct buf *ob, const struct buf *text, int flags, void *opaque) {\
        HandleScope scope;                                                     \
                                                                               \
        /*Convert arguments*/                                                  \
        Local<Value> args [2] = {toString(text), Int(flags)};                  \
                                                                               \
        /*Call it!*/                                                           \
        TryCatch trycatch;                                                     \
        Local<Value> ret = ((RendererData*)opaque)->CPPFUNC->CallAsFunction(Context::GetCurrent()->Global(), 2, args);\
        if (trycatch.HasCaught())                                              \
            V8_THROW(trycatch.Exception());                                    \
        /*Convert the result back*/                                            \
        if (ret->IsFalse()) return BINDER_RETURN_NULL_##RET;                   \
        putToBuf(ob, ret);                                                     \
        return BINDER_RETURN_##RET;                                            \
    }

// FORWARDERS (forward a Sundown call to its original C++ renderer)

#define BUF1_FORWARDER(CPPFUNC, RET)                                           \
    static RET CPPFUNC##_forwarder(struct buf *ob, void *opaque) {             \
        RendererData* rend = (RendererData*)opaque;                            \
        return ((RET(*)(struct buf *ob, void *opaque))rend->CPPFUNC##_orig)(   \
                ob,                                                            \
                rend->CPPFUNC##_opaque);                                       \
    }

#define BUF2_FORWARDER(CPPFUNC, RET)                                           \
    static RET CPPFUNC##_forwarder(struct buf *ob, const struct buf *text, void *opaque) {\
        RendererData* rend = (RendererData*)opaque;                            \
        return ((RET(*)(struct buf *ob, const struct buf *text, void *opaque))rend->CPPFUNC##_orig)(\
                ob, text,                                                      \
                rend->CPPFUNC##_opaque);                                       \
    }

#define BUF2INT_FORWARDER(CPPFUNC, RET)                                        \
    static RET CPPFUNC##_forwarder(struct buf *ob, const struct buf *text, int flags, void *opaque) {\
        RendererData* rend = (RendererData*)opaque;                            \
        return ((RET(*)(struct buf *ob, const struct buf *text, int flags, void *opaque))rend->CPPFUNC##_orig)(\
                ob, text, flags,                                               \
                rend->CPPFUNC##_opaque);                                       \
    }

#define BUF3_FORWARDER(CPPFUNC, RET)                                           \
    static RET CPPFUNC##_forwarder(struct buf *ob, const struct buf *text, const struct buf *lang, void *opaque) {\
        RendererData* rend = (RendererData*)opaque;                            \
        return ((RET(*)(struct buf *ob, const struct buf *text, const struct buf *lang, void *opaque))rend->CPPFUNC##_orig)(\
                ob, text, lang,                                                \
                rend->CPPFUNC##_opaque);                                       \
    }

#define BUF4_FORWARDER(CPPFUNC, RET)                                           \
    static RET CPPFUNC##_forwarder(struct buf *ob, const struct buf *link, const struct buf *title, const struct buf *cont, void *opaque) {\
        RendererData* rend = (RendererData*)opaque;                            \
        return ((RET(*)(struct buf *ob, const struct buf *link, const struct buf *title, const struct buf *cont, void *opaque))rend->CPPFUNC##_orig)(\
                ob, link, title, cont,                                         \
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
        return scope.Close(*(inst->CPPFUNC));                                  \
    } V8_WRAP_END()

#define _RENDFUNC_SETTER(CPPFUNC, SIGNATURE)                                   \
    static V8_SETTER(CPPFUNC##_setter) {                                       \
        V8_UNWRAP(RendererWrap, info)                                          \
        if (!Bool(value)) {                                                    \
            inst->CPPFUNC.Clear();                                             \
            return;                                                            \
        }                                                                      \
        if (!value->IsObject()) V8_THROW(TypeErr("Value must be a function!"));\
        Local<Object> obj = Obj(value);                                        \
        if (!obj->IsCallable()) V8_THROW(TypeErr("Value must be a function!"));\
                                                                               \
        inst->CPPFUNC = obj;                                                   \
    } V8_WRAP_END_NR()

// The final macros (defining)

#define RENDFUNC_DEF(CPPFUNC, SIGBASE, RET)                                    \
    protected: Persisted<Object> CPPFUNC;                                      \
    public:                                                                    \
        _RENDFUNC_GETTER(CPPFUNC)                                              \
        _RENDFUNC_SETTER(CPPFUNC, RET##_##SIGBASE)                             \
                                                                               \
        SIGBASE##_FORWARDER(CPPFUNC,RET) SIGBASE##_BINDER(CPPFUNC,RET)

#define RENDFUNC_DATA(CPPFUNC)                                                 \
    void* CPPFUNC##_opaque;                                                    \
    void* CPPFUNC##_orig;                                                      \
    Persisted<Object> CPPFUNC;

// The final macros (wrapping / making)

#define RENDFUNC_WRAP(CPPFUNC, SIGBASE, RET)                                   \
    if (cb->CPPFUNC)                                                           \
        CPPFUNC = jsFunction((void*)cb->CPPFUNC, RET##_##SIGBASE, &SIGBASE##_wrapper_##RET, opaque);

#define RENDFUNC_MAKE(CPPFUNC, SIGBASE, RET)                                   \
    if (!CPPFUNC.IsEmpty()) {                                                  \
        opaque->CPPFUNC = CPPFUNC;                                             \
        if (setCppFunction((void**)&(opaque->CPPFUNC##_orig), (void**)&(opaque->CPPFUNC##_opaque), *CPPFUNC, RET##_##SIGBASE))\
            cb->CPPFUNC = &CPPFUNC##_forwarder;                                \
        else cb->CPPFUNC = &CPPFUNC##_binder;                                  \
    }
        
// Forward declaration, to make things work
class Markdown;

class RendererData {
public:
  RENDFUNC_DATA(hrule)
};

class RendererWrap: public ObjectWrap {
public:
    V8_CL_WRAPPER("robotskirt::RendererWrap")
    RendererWrap() {}
    virtual ~RendererWrap() {}
    V8_CL_CTOR(RendererWrap, 0) {
        inst = new RendererWrap();
    } V8_CL_CTOR_END()
    void makeRenderer(sd_callbacks* cb, RendererData* opaque) {
        memset(cb, 0, sizeof(*cb));
        RENDFUNC_MAKE(hrule, BUF1, void)
    }
protected:
    void wrapRenderer(sd_callbacks* cb, RendFuncData* opaque) {
        RENDFUNC_WRAP(hrule, BUF1, void)
    }

// Renderer functions
RENDFUNC_DEF(hrule, BUF1, void)
};

class HtmlRendererWrap: public RendererWrap {
public:
    V8_CL_WRAPPER("robotskirt::HtmlRendererWrap")
    HtmlRendererWrap(int flags): data(new HtmlRendFuncData) {
        //FIXME:expose options (Read-only)
        sd_callbacks cb;
        sdhtml_renderer(&cb, (html_renderopt*)data->ptr(), flags);
        wrapRenderer(&cb, data);
    }
    ~HtmlRendererWrap() {
        data->unref();
    }
    V8_CL_CTOR(HtmlRendererWrap, 0) {
        //Extract arguments
        unsigned int flags = 0;
        if (args.Length() >= 1) {
            flags = CheckFlags(args[0]);
        }

        inst = new HtmlRendererWrap(flags);
    } V8_CL_CTOR_END()

    V8_CL_GETTER(HtmlRendererWrap, Flags) {
        return scope.Close(Int(((html_renderopt*)inst->data->ptr())->flags));
    } V8_WRAP_END()
protected:
    HtmlRendFuncData* const data;
};



////////////////////////////////////////////////////////////////////////////////
// MARKDOWN CLASS DECLARATION
////////////////////////////////////////////////////////////////////////////////

class Markdown: public ObjectWrap {
public:
    V8_CL_WRAPPER("robotskirt::Markdown")
    Markdown(RendererWrap* renderer, unsigned int extensions, size_t max_nesting):
            max_nesting_(max_nesting), extensions_(extensions) {
        markdown = makeMarkdown(renderer, &cb, &opaque, extensions, max_nesting);
    }
    ~Markdown() {
        sd_markdown_free(markdown);
    }
    V8_CL_CTOR(Markdown, 1) {
        //Check & extract arguments
        if (!args[0]->IsObject()) V8_THROW(TypeErr("You must provide a Renderer!"));
        Local<Object> obj = Obj(args[0]);

        if (!GetTemplate("robotskirt::RendererWrap")->HasInstance(obj))
            V8_THROW(TypeErr("You must provide a Renderer!"));
        RendererWrap* rend = Unwrap<RendererWrap>(obj);

        unsigned int extensions = 0;
        size_t max_nesting = DEFAULT_MAX_NESTING;
        if (args.Length()>=2) {
            extensions = CheckUFlags(args[1]);
            if (args.Length()>=3) {
                max_nesting = Uint(args[2]);
            }
        }

        inst = new Markdown(rend, extensions, max_nesting);
    } V8_CL_CTOR_END()

    V8_CL_GETTER(Markdown, MaxNesting) {
        return scope.Close(Uint(inst->max_nesting_));
    } V8_WRAP_END()
    V8_CL_GETTER(Markdown, Extensions) {
        return scope.Close(Uint(inst->extensions_));
    } V8_WRAP_END()

    //And the most important function(s)...
    V8_CL_CALLBACK(Markdown, RenderSync, 1) {
        //Extract input
        String::Utf8Value input (args[0]);

        //Prepare
        BufWrap out (bufnew(OUTPUT_UNIT));

        //GO!!
        sd_markdown_render(*out,
                           reinterpret_cast<const unsigned char*>(*input),
                           input.length(),
                           inst->markdown);

        //Finish
        return scope.Close(toString(*out));
    } V8_WRAP_END()
    //TODO: async version of Render(...)
protected:
    sd_markdown* markdown;
    sd_callbacks cb;
    RendererData opaque;
    size_t const max_nesting_;
    int const extensions_;
private:
    static sd_markdown* makeMarkdown(RendererWrap* rend,
            sd_callbacks* cb, RendererData* opaque,
            unsigned int extensions, size_t max_nesting) {
        rend->makeRenderer(cb, opaque);
        return sd_markdown_new(extensions, max_nesting, cb, opaque);
    }
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
// OTHER UTILITIES (version stuff, etc.)
////////////////////////////////////////////////////////////////////////////////

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

    V8_DEF_METHOD(Markdown, RenderSync, "renderSync");

    StoreTemplate("robotskirt::Markdown", prot);
} NODE_DEF_TYPE_END()

NODE_DEF_TYPE(FunctionData, "NativeFunction") {
    V8_DEF_METHOD(FunctionData, ToString, "toString");
    V8_DEF_METHOD(FunctionData, ToString, "inspect");

    prot->InstanceTemplate()->SetCallAsFunctionHandler(FunctionData::Call);
    StoreTemplate("robotskirt::FunctionData", prot);
} NODE_DEF_TYPE_END()

NODE_DEF_MAIN() {
    //Initialize classes
    initRendererWrap(target);
    initHtmlRendererWrap(target);
    initMarkdown(target);
    initFunctionData(target);

    //Version class & hash
    initVersion(target);
    Local<Object> versions = Obj();
    versions->Set(Symbol("sundown"), SundownVersion());
    versions->Set(Symbol("robotskirt"), (new Version(2,2,3))->Wrapped());
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

