#include "v8u.hpp"
#include "version.hpp"
#include <node_buffer.h>

#include <string>
#include <cstring>

extern "C" {
  #include "markdown.h"
  #include "html.h"
  #include "houdini.h"
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
    explicit BufWrap(buf* buf): buf_(buf) {}
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
inline Handle<Value> toString(const buf* buf) {
    if (!buf) return Null();
    return String::New(reinterpret_cast<const char*>(buf->data), buf->size);
}
inline void makeBuf(buf*& target, String::Utf8Value*& txt, Local<Value> value) {
  if (value->IsUndefined() || value->IsNull()) return;
  txt = new String::Utf8Value(value);
  target = new buf;
  target->data = (uint8_t*)(**txt);
  target->size = target->asize = txt->length();
  target->unit = 0;
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
    static V8_CALLBACK(ToString) {
        return scope.Close(Str("<Native function>"));
    } V8_CALLBACK_END()
    static Handle<Value> Call(const Arguments& args) {
        HandleScope scope;
        V8_UNWRAP(FunctionData, args)
        return inst->wrapper_(inst, args);
    }
    
    NODE_DEF_TYPE("NativeFunction") {
        V8_DEF_METHOD(ToString, "toString");
        V8_DEF_METHOD(ToString, "inspect");

        prot->InstanceTemplate()->SetCallAsFunctionHandler(Call);
        StoreTemplate("robotskirt::FunctionData", prot);
    } NODE_DEF_TYPE_END()
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
        if(args.Length()<1) return ThrowException(RangeErr("Not enough arguments."));\
        String::Utf8Value* texts = NULL;                                       \
        buf* text = NULL;                                                      \
        makeBuf(text, texts, args[0]);                                         \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, void*))inst->getFunction())\
                (*ob, text,  inst->getOpaque());                               \
        if (texts != NULL) { delete texts; delete text; }                      \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF2)

#define BUF2INT_WRAPPER(RET)                                                   \
    Handle<Value> BUF2INT_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        if(args.Length()<2) return ThrowException(RangeErr("Not enough arguments."));\
        String::Utf8Value* texts = NULL;                                       \
        buf* text = NULL;                                                      \
        makeBuf(text, texts, args[0]);                                         \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, int, void*))inst->getFunction())\
                (*ob, text, Int(args[1]),  inst->getOpaque());                 \
        if (texts != NULL) { delete texts; delete text; }                      \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF2INT)

#define BUF3_WRAPPER(RET)                                                      \
    Handle<Value> BUF3_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        if(args.Length()<2) return ThrowException(RangeErr("Not enough arguments."));\
        String::Utf8Value* texts = NULL;                                       \
        buf* text = NULL;                                                      \
        makeBuf(text, texts, args[0]);                                         \
                                                                               \
        String::Utf8Value* langs = NULL;                                       \
        buf* lang = NULL;                                                      \
        makeBuf(lang, langs, args[1]);                                         \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, const buf*, void*))inst->getFunction())\
                (*ob, text, lang,  inst->getOpaque());                         \
        if (texts != NULL) { delete texts; delete text; }                      \
        if (langs != NULL) { delete langs; delete lang; }                      \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF3)

#define BUF4_WRAPPER(RET)                                                      \
    Handle<Value> BUF4_wrapper_##RET(FunctionData* inst, const Arguments& args) {\
        if(args.Length()<3) return ThrowException(RangeErr("Not enough arguments."));\
        String::Utf8Value* links = NULL;                                       \
        buf* link = NULL;                                                      \
        makeBuf(link, links, args[0]);                                         \
                                                                               \
        String::Utf8Value* titles = NULL;                                      \
        buf* title = NULL;                                                     \
        makeBuf(title, titles, args[1]);                                       \
                                                                               \
        String::Utf8Value* conts = NULL;                                       \
        buf* cont = NULL;                                                      \
        makeBuf(cont, conts, args[2]);                                         \
                                                                               \
        BufWrap ob (bufnew(W_OUTPUT_UNIT));                                    \
        WRAPPER_CALL_##RET() ((RET(*)(buf*, const buf*, const buf*, const buf*, void*))inst->getFunction())\
                (*ob, link, title, cont,  inst->getOpaque());                  \
        if (links != NULL) { delete links; delete link; }                      \
        if (titles != NULL) { delete titles; delete title; }                   \
        if (conts != NULL) { delete conts; delete cont; }                      \
        WRAPPER_POST_CALL_##RET()                                              \
        return toString(*ob);                                                  \
    }
WRAPPERS(BUF4)

// BINDERS (call a JS function from CPP)

#define BINDER_RETURN_void
#define BINDER_RETURN_int  1

#define BINDER_RETURN_NULL_void
#define BINDER_RETURN_NULL_int  0

#define BUF1_BINDER(CPPFUNC, RET)                                              \
    static RET CPPFUNC##_binder(struct buf *ob, void *opaque) {                \
        HandleScope scope;                                                     \
                                                                               \
        /*Convert arguments*/                                                  \
        Handle<Value> args [0];                                                \
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
        Handle<Value> args [1] = {toString(text)};                             \
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
        Handle<Value> args [2] = {toString(text), Int(flags)};                 \
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

#define BUF3_BINDER(CPPFUNC, RET)                                              \
    static RET CPPFUNC##_binder(struct buf *ob, const struct buf *text, const struct buf *lang, void *opaque) {\
        HandleScope scope;                                                     \
                                                                               \
        /*Convert arguments*/                                                  \
        Handle<Value> args [2] = {toString(text), toString(lang)};             \
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

#define BUF4_BINDER(CPPFUNC, RET)                                              \
    static RET CPPFUNC##_binder(struct buf *ob, const struct buf *link, const struct buf *title, const struct buf *cont, void *opaque) {\
        HandleScope scope;                                                     \
                                                                               \
        /*Convert arguments*/                                                  \
        Handle<Value> args [3] = {toString(link), toString(title), toString(cont)};\
                                                                               \
        /*Call it!*/                                                           \
        TryCatch trycatch;                                                     \
        Local<Value> ret = ((RendererData*)opaque)->CPPFUNC->CallAsFunction(Context::GetCurrent()->Global(), 3, args);\
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
    } V8_GETTER_END()

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
    } V8_SETTER_END()

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

//Special case for autolink
#define RENDFUNC_MAKE_AL(CPPFUNC, SIGBASE, RET)                                \
    if (!CPPFUNC.IsEmpty()) {                                                  \
        opaque->CPPFUNC = CPPFUNC;                                             \
        if (setCppFunction((void**)&(opaque->CPPFUNC##_orig), (void**)&(opaque->CPPFUNC##_opaque), *CPPFUNC, RET##_##SIGBASE))\
            cb->CPPFUNC = (int(*)(buf*,const buf*,mkd_autolink,void*))&CPPFUNC##_forwarder;\
        else cb->CPPFUNC = (int(*)(buf*,const buf*,mkd_autolink,void*))&CPPFUNC##_binder;\
    }

//Define V8 accessors
#define RENDFUNC_V8_DEF(NAME, CPPFUNC)                                         \
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol(NAME),             \
            RendererWrap::CPPFUNC##_getter, RendererWrap::CPPFUNC##_setter);

// Forward declaration, to make things work
class Markdown;

class RendererData {
public:
    RENDFUNC_DATA(blockcode)
    RENDFUNC_DATA(blockquote)
    RENDFUNC_DATA(blockhtml)
    RENDFUNC_DATA(header)
    RENDFUNC_DATA(hrule)
    RENDFUNC_DATA(list)
    RENDFUNC_DATA(listitem)
    RENDFUNC_DATA(paragraph)
    RENDFUNC_DATA(table)
    RENDFUNC_DATA(table_row)
    RENDFUNC_DATA(table_cell)
    RENDFUNC_DATA(autolink)
    RENDFUNC_DATA(codespan)
    RENDFUNC_DATA(double_emphasis)
    RENDFUNC_DATA(emphasis)
    RENDFUNC_DATA(image)
    RENDFUNC_DATA(linebreak)
    RENDFUNC_DATA(link)
    RENDFUNC_DATA(raw_html_tag)
    RENDFUNC_DATA(triple_emphasis)
    RENDFUNC_DATA(strikethrough)
    RENDFUNC_DATA(superscript)
    RENDFUNC_DATA(entity)
    RENDFUNC_DATA(normal_text)
    RENDFUNC_DATA(doc_header)
    RENDFUNC_DATA(doc_footer)
};

class RendererWrap: public ObjectWrap {
public:
    V8_CL_WRAPPER("robotskirt::RendererWrap")
    RendererWrap() {}
    virtual ~RendererWrap() {}
    V8_CL_CTOR(RendererWrap) {
        inst = new RendererWrap();
    } V8_CL_CTOR_END()
    void makeRenderer(sd_callbacks* cb, RendererData* opaque) {
        memset(cb, 0, sizeof(*cb));
        RENDFUNC_MAKE(blockcode, BUF3, void)
        RENDFUNC_MAKE(blockquote, BUF2, void)
        RENDFUNC_MAKE(blockhtml, BUF2, void)
        RENDFUNC_MAKE(header, BUF2INT, void)
        RENDFUNC_MAKE(hrule, BUF1, void)
        RENDFUNC_MAKE(list, BUF2INT, void)
        RENDFUNC_MAKE(listitem, BUF2INT, void)
        RENDFUNC_MAKE(paragraph, BUF2, void)
        RENDFUNC_MAKE(table, BUF3, void)
        RENDFUNC_MAKE(table_row, BUF2, void)
        RENDFUNC_MAKE(table_cell, BUF2INT, void)
        RENDFUNC_MAKE_AL(autolink, BUF2INT, int)
        RENDFUNC_MAKE(codespan, BUF2, int)
        RENDFUNC_MAKE(double_emphasis, BUF2, int)
        RENDFUNC_MAKE(emphasis, BUF2, int)
        RENDFUNC_MAKE(image, BUF4, int)
        RENDFUNC_MAKE(linebreak, BUF1, int)
        RENDFUNC_MAKE(link, BUF4, int)
        RENDFUNC_MAKE(raw_html_tag, BUF2, int)
        RENDFUNC_MAKE(triple_emphasis, BUF2, int)
        RENDFUNC_MAKE(strikethrough, BUF2, int)
        RENDFUNC_MAKE(superscript, BUF2, int)
        RENDFUNC_MAKE(entity, BUF2, void)
        RENDFUNC_MAKE(normal_text, BUF2, void)
        RENDFUNC_MAKE(doc_header, BUF1, void)
        RENDFUNC_MAKE(doc_footer, BUF1, void)
    }
    NODE_DEF_TYPE("Renderer") {
        RENDFUNC_V8_DEF("blockcode", blockcode)
        RENDFUNC_V8_DEF("blockquote", blockquote)
        RENDFUNC_V8_DEF("blockhtml", blockhtml)
        RENDFUNC_V8_DEF("header", header)
        RENDFUNC_V8_DEF("hrule", hrule)
        RENDFUNC_V8_DEF("list", list)
        RENDFUNC_V8_DEF("listitem", listitem)
        RENDFUNC_V8_DEF("paragraph", paragraph)
        RENDFUNC_V8_DEF("table", table)
        RENDFUNC_V8_DEF("table_row", table_row)
        RENDFUNC_V8_DEF("table_cell", table_cell)
        RENDFUNC_V8_DEF("autolink", autolink)
        RENDFUNC_V8_DEF("codespan", codespan)
        RENDFUNC_V8_DEF("double_emphasis", double_emphasis)
        RENDFUNC_V8_DEF("emphasis", emphasis)
        RENDFUNC_V8_DEF("image", image)
        RENDFUNC_V8_DEF("linebreak", linebreak)
        RENDFUNC_V8_DEF("link", link)
        RENDFUNC_V8_DEF("raw_html_tag", raw_html_tag)
        RENDFUNC_V8_DEF("triple_emphasis", triple_emphasis)
        RENDFUNC_V8_DEF("strikethrough", strikethrough)
        RENDFUNC_V8_DEF("superscript", superscript)
        RENDFUNC_V8_DEF("entity", entity)
        RENDFUNC_V8_DEF("normal_text", normal_text)
        RENDFUNC_V8_DEF("doc_header", doc_header)
        RENDFUNC_V8_DEF("doc_footer", doc_footer)

        StoreTemplate("robotskirt::RendererWrap", prot);
    } NODE_DEF_TYPE_END()
protected:
    void wrapRenderer(sd_callbacks* cb, RendFuncData* opaque) {
        RENDFUNC_WRAP(blockcode, BUF3, void)
        RENDFUNC_WRAP(blockquote, BUF2, void)
        RENDFUNC_WRAP(blockhtml, BUF2, void)
        RENDFUNC_WRAP(header, BUF2INT, void)
        RENDFUNC_WRAP(hrule, BUF1, void)
        RENDFUNC_WRAP(list, BUF2INT, void)
        RENDFUNC_WRAP(listitem, BUF2INT, void)
        RENDFUNC_WRAP(paragraph, BUF2, void)
        RENDFUNC_WRAP(table, BUF3, void)
        RENDFUNC_WRAP(table_row, BUF2, void)
        RENDFUNC_WRAP(table_cell, BUF2INT, void)
        RENDFUNC_WRAP(autolink, BUF2INT, int)
        RENDFUNC_WRAP(codespan, BUF2, int)
        RENDFUNC_WRAP(double_emphasis, BUF2, int)
        RENDFUNC_WRAP(emphasis, BUF2, int)
        RENDFUNC_WRAP(image, BUF4, int)
        RENDFUNC_WRAP(linebreak, BUF1, int)
        RENDFUNC_WRAP(link, BUF4, int)
        RENDFUNC_WRAP(raw_html_tag, BUF2, int)
        RENDFUNC_WRAP(triple_emphasis, BUF2, int)
        RENDFUNC_WRAP(strikethrough, BUF2, int)
        RENDFUNC_WRAP(superscript, BUF2, int)
        RENDFUNC_WRAP(entity, BUF2, void)
        RENDFUNC_WRAP(normal_text, BUF2, void)
        RENDFUNC_WRAP(doc_header, BUF1, void)
        RENDFUNC_WRAP(doc_footer, BUF1, void)
    }

// Renderer functions
RENDFUNC_DEF(blockcode, BUF3, void)
RENDFUNC_DEF(blockquote, BUF2, void)
RENDFUNC_DEF(blockhtml, BUF2, void)
RENDFUNC_DEF(header, BUF2INT, void)
RENDFUNC_DEF(hrule, BUF1, void)
RENDFUNC_DEF(list, BUF2INT, void)
RENDFUNC_DEF(listitem, BUF2INT, void)
RENDFUNC_DEF(paragraph, BUF2, void)
RENDFUNC_DEF(table, BUF3, void)
RENDFUNC_DEF(table_row, BUF2, void)
RENDFUNC_DEF(table_cell, BUF2INT, void)
RENDFUNC_DEF(autolink, BUF2INT, int)
RENDFUNC_DEF(codespan, BUF2, int)
RENDFUNC_DEF(double_emphasis, BUF2, int)
RENDFUNC_DEF(emphasis, BUF2, int)
RENDFUNC_DEF(image, BUF4, int)
RENDFUNC_DEF(linebreak, BUF1, int)
RENDFUNC_DEF(link, BUF4, int)
RENDFUNC_DEF(raw_html_tag, BUF2, int)
RENDFUNC_DEF(triple_emphasis, BUF2, int)
RENDFUNC_DEF(strikethrough, BUF2, int)
RENDFUNC_DEF(superscript, BUF2, int)
RENDFUNC_DEF(entity, BUF2, void)
RENDFUNC_DEF(normal_text, BUF2, void)
RENDFUNC_DEF(doc_header, BUF1, void)
RENDFUNC_DEF(doc_footer, BUF1, void)
};



////////////////////////////////////////////////////////////////////////////////
// SUNDOWN BUNDLED RENDERERS ([X]HTML)
////////////////////////////////////////////////////////////////////////////////

class HtmlRendererWrap: public RendererWrap {
public:
    V8_CL_WRAPPER("robotskirt::HtmlRendererWrap")
    HtmlRendererWrap(unsigned int flags): data(new HtmlRendFuncData) {
        //FIXME:expose options (Read-only)
        sd_callbacks cb;
        sdhtml_renderer(&cb, (html_renderopt*)data->ptr(), flags);
        wrapRenderer(&cb, data);
    }
    ~HtmlRendererWrap() {
        data->unref();
    }
    V8_CL_CTOR(HtmlRendererWrap) {
        //Extract arguments
        unsigned int flags = 0;
        if (args.Length() >= 1) {
            flags = CheckUFlags(args[0]);
        }

        inst = new HtmlRendererWrap(flags);
    } V8_CL_CTOR_END()

    V8_CL_GETTER(HtmlRendererWrap, Flags) {
        return scope.Close(Uint(((html_renderopt*)inst->data->ptr())->flags));
    } V8_GETTER_END()
    
    NODE_DEF_TYPE("HtmlRenderer") {
        V8_INHERIT("robotskirt::RendererWrap");

        V8_DEF_RPROP(Flags, "flags");

        StoreTemplate("robotskirt::HtmlRendererWrap", prot);
    } NODE_DEF_TYPE_END()
protected:
    HtmlRendFuncData* const data;
};



////////////////////////////////////////////////////////////////////////////////
// MARKDOWN CLASS DECLARATION
////////////////////////////////////////////////////////////////////////////////

//Forward the "constructor" at the subclasses
inline Markdown* newMarkdownWrap(RendererWrap* renderer, unsigned int extensions, size_t max_nesting);
inline Markdown* newStdMarkdown(unsigned int extensions, unsigned int htmlflags, size_t max_nesting);

//Base Markdown class, doesn't contain logic to store renderer data;
//this is specific to subclasses
class Markdown: public ObjectWrap {
public:
    V8_CL_WRAPPER("robotskirt::Markdown")
    //Here, it's important that the destructor gets declared virtual
    virtual ~Markdown() {
        sd_markdown_free(markdown);
    }
    V8_CL_CTOR(Markdown) {
        //Check & extract arguments
        CheckArguments(1, args);
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

        inst = newMarkdownWrap(rend, extensions, max_nesting);
    } V8_CL_CTOR_END()
    static V8_CALLBACK(MakeStandard) {
        unsigned int extensions = 0;
        unsigned int htmlflags = 0;
        size_t max_nesting = DEFAULT_MAX_NESTING;
        if (args.Length()>=1) {
            extensions = CheckUFlags(args[0]);
            if (args.Length()>=2) {
                htmlflags = CheckUFlags(args[1]);
                if (args.Length()>=3) {
                    max_nesting = Uint(args[2]);
                }
            }
        }

        return newStdMarkdown(extensions, htmlflags, max_nesting)->Wrapped();
    } V8_CALLBACK_END()

    V8_CL_GETTER(Markdown, MaxNesting) {
        return scope.Close(Uint(inst->max_nesting_));
    } V8_GETTER_END()
    V8_CL_GETTER(Markdown, Extensions) {
        return scope.Close(Uint(inst->extensions_));
    } V8_GETTER_END()

    static V8_S_CALLBACK(RenderSync) {
        fprintf(stderr, "renderSync() is DEPRECATED, please use render() instead.\n");
        return Render(args);
    }
    //And the most important function(s)...
    V8_CL_CALLBACK(Markdown, Render) {
        //Extract input
        CheckArguments(1, args);
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
    } V8_CALLBACK_END()

    NODE_DEF_TYPE("Markdown") {
        V8_DEF_RPROP(Extensions, "extensions");
        V8_DEF_RPROP(MaxNesting, "maxNesting");

        V8_DEF_METHOD(Render, "render");
        V8_DEF_METHOD(RenderSync, "renderSync");
        
        prot->GetFunction()->Set(Symbol("std"), Func(MakeStandard)->GetFunction());

        StoreTemplate("robotskirt::Markdown", prot);
    } NODE_DEF_TYPE_END()
protected:
    sd_markdown* markdown;
    sd_callbacks cb;
    size_t max_nesting_;
    int extensions_;
};

// A markdown parser holding JS-wrapped Renderer data.
class MarkdownWrap : public Markdown {
public:
    MarkdownWrap(RendererWrap* renderer, unsigned int extensions, size_t max_nesting) {
        max_nesting_ = max_nesting;
        extensions_ = extensions;
        markdown = makeMarkdown(renderer, &cb, &opaque, extensions, max_nesting);
    }
    //FIXME: is deallocation correct?
protected:
    RendererData opaque;
private:
    static inline sd_markdown* makeMarkdown(RendererWrap* rend,
            sd_callbacks* cb, RendererData* opaque,
            unsigned int extensions, size_t max_nesting) {
        rend->makeRenderer(cb, opaque);
        return sd_markdown_new(extensions, max_nesting, cb, opaque);
    }
};

// A markdown parser holding a standard Sundown HTML renderer
class StdMarkdown : public Markdown {
public:
    StdMarkdown(unsigned int extensions, unsigned int htmlflags, size_t max_nesting) {
        max_nesting_ = max_nesting;
        extensions_ = extensions;
        //Create HTML renderer
        sdhtml_renderer(&cb, &options, htmlflags);
        //Create the Markdown parser
        markdown = sd_markdown_new(extensions, max_nesting, &cb, &options);
    }
protected:
    html_renderopt options;
};

//Now we can define the "constructors"
inline Markdown* newMarkdownWrap(RendererWrap* renderer, unsigned int extensions, size_t max_nesting) {
    return new MarkdownWrap(renderer, extensions, max_nesting);
}
inline Markdown* newStdMarkdown(unsigned int extensions, unsigned int htmlflags, size_t max_nesting) {
    return new StdMarkdown(extensions, htmlflags, max_nesting);
}



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
// OTHER UTILITIES (version stuff, complementary functionality, etc.)
////////////////////////////////////////////////////////////////////////////////

Local<Object> SundownVersion() {
    return (new Version(SUNDOWN_VER_MAJOR,
                        SUNDOWN_VER_MINOR,
                        SUNDOWN_VER_REVISION))->Wrapped();
}

//SUBMODULE: Houdini, the escapist (a part comes bundled with Sundown)
//NOTE: it also UNescapes, but that breaks the joke in the name so nevermind.
namespace houdini {
  #define HOUDINI_OUTPUT_UNIT OUTPUT_UNIT
  #define HOUDINI_DEFAULT_TO_SECURE true

  #define HOUDINI_STANDARD_WRAPPER(IDENTIFIER, FUNCTION)                       \
    V8_S_CALLBACK(IDENTIFIER) {                                                \
      HandleScope scope;                                                       \
      if (args.Length()<1) return ThrowException(RangeErr("One argument needed."));\
      String::Utf8Value input (args[0]);                                       \
                                                                               \
      BufWrap out (bufnew(HOUDINI_OUTPUT_UNIT));                               \
      houdini_##FUNCTION(*out, (const unsigned char*)(*input), input.length());\
      return scope.Close(toString(*out));                                      \
    }

  //JS [un]escaping
  HOUDINI_STANDARD_WRAPPER(EscapeJs, escape_js)
  HOUDINI_STANDARD_WRAPPER(UnescapeJs, unescape_js)
  
  //URL [un]escaping
  HOUDINI_STANDARD_WRAPPER(EscapeUrl, escape_url)
  HOUDINI_STANDARD_WRAPPER(UnescapeUrl, unescape_url)

  //URI [un]escaping
  HOUDINI_STANDARD_WRAPPER(EscapeUri, escape_uri)
  HOUDINI_STANDARD_WRAPPER(UnescapeUri, unescape_uri)

  //HTML [un]escaping
  V8_S_CALLBACK(EscapeHtml) {
    HandleScope scope;
    if (args.Length()<1) return ThrowException(RangeErr("One argument needed."));
    String::Utf8Value input (args[0]);
    bool secure = HOUDINI_DEFAULT_TO_SECURE;
    if (args.Length()>=2) secure = Bool(args[1]);

    BufWrap out (bufnew(HOUDINI_OUTPUT_UNIT));
    houdini_escape_html0(*out, (const unsigned char*)(*input), input.length(), secure);
    return scope.Close(toString(*out));
  }
  HOUDINI_STANDARD_WRAPPER(UnescapeHtml, unescape_html)
  
  //Additional HREF escaping
  HOUDINI_STANDARD_WRAPPER(EscapeHref, escape_href)
  
  //Additional XML escaping
  HOUDINI_STANDARD_WRAPPER(EscapeXml, escape_xml)

  //The initializer
  NODE_DEF(init) {
    target->Set(Symbol("escapeJS"), Func(EscapeJs)->GetFunction());
    target->Set(Symbol("unescapeJS"), Func(UnescapeJs)->GetFunction());

    target->Set(Symbol("escapeURL"), Func(EscapeUrl)->GetFunction());
    target->Set(Symbol("unescapeURL"), Func(UnescapeUrl)->GetFunction());

    target->Set(Symbol("escapeURI"), Func(EscapeUri)->GetFunction());
    target->Set(Symbol("unescapeURI"), Func(UnescapeUri)->GetFunction());

    target->Set(Symbol("escapeHTML"), Func(EscapeHtml)->GetFunction());
    target->Set(Symbol("unescapeHTML"), Func(UnescapeHtml)->GetFunction());

    target->Set(Symbol("escapeHREF"), Func(EscapeHref)->GetFunction());

    target->Set(Symbol("escapeXML"), Func(EscapeXml)->GetFunction());
  }
};

//SMARTYPANTS (Sundown implementation)
#define SMARTYPANTS_OUTPUT_UNIT OUTPUT_UNIT
V8_CALLBACK(SmartypantsHtml) {
  //Extract input
  CheckArguments(1, args);
  String::Utf8Value input (args[0]);
  
  //Prepare
  BufWrap out (bufnew(SMARTYPANTS_OUTPUT_UNIT));
  
  //GO!!
  sdhtml_smartypants(*out,
                     reinterpret_cast<const unsigned char*>(*input),
                     input.length());
  
  //Finish
  return scope.Close(toString(*out));
} V8_CALLBACK_END()



////////////////////////////////////////////////////////////////////////////////
// MODULE DECLARATION
////////////////////////////////////////////////////////////////////////////////

NODE_DEF_MAIN() {
    //Initialize classes
    RendererWrap::init(target);
    HtmlRendererWrap::init(target);
    Markdown::init(target);
    FunctionData::init(target);

    //Version class & hash
    Version::init(target);
    Local<Object> versions = Obj();
    versions->Set(Symbol("sundown"), SundownVersion());
    versions->Set(Symbol("robotskirt"), (new Version(2,6,1))->Wrapped());
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

    //SUBMODULE: Houdini, the escapist
    Local<Object> houdiniL = Obj();
    houdini::init(houdiniL);
    target->Set(Symbol("houdini"), houdiniL);
    
    //SMARTYPANTS
    target->Set(Symbol("smartypantsHtml"), Func(SmartypantsHtml)->GetFunction());
} NODE_DEF_MAIN_END(robotskirt)

}

