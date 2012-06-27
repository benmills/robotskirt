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

#define OUTPUT_UNIT 64
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
  Handle<Value> NG_JS_ARGS[3] = {                             \
    NG_SLOW_BUFFER->handle_,                                  \
    Integer::New(Buffer::Length(NG_SLOW_BUFFER)),             \
    Integer::New(0)                                           \
  };                                                          \
                                                              \
  NG_FAST_BUFFER = NG_JS_BUFFER->NewInstance(3, NG_JS_ARGS);

// JS arguments

void CheckArguments(int min, const Arguments& args) {
    if (args.Length() < min)
        throw Exception::RangeError(String::New("Not enough arguments."));
}

// V8 exception wrapping

#define V8_WRAP_START()                                                        \
  HandleScope scope;                                                           \
  try {

#define V8_WRAP_END()                                                          \
  } catch (Handle<Value> err) {                                                \
    return ThrowException(err);                                                \
  } catch (exception err) {                                                    \
    return ThrowException(Exception::Error(String::New(err.what())));          \
  }                                                                            \
}

#define V8_WRAP_END_NR()                                                       \
  } catch (Handle<Value> err) {                                                \
    ThrowException(err);                                                       \
  } catch (exception err) {                                                    \
    ThrowException(Exception::Error(String::New(err.what())));                 \
  }                                                                            \
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
// OTHER JS FUNCTIONS
////////////////////////////////////////////////////////////////////////////////

class Version: public ObjectWrap {
private:
    int major_, minor_, revision_;
public:
    int getMajor() {return major_;}
    int getMinor() {return minor_;}
    int getRevision() {return revision_;}
    void setMajor(int major) {major_=major;}
    void setMinor(int minor) {minor_=minor;}
    void setRevision(int revision) {revision_=revision;}
    string toString() {
        stringstream ret;
        ret << major_ << "." << minor_ << "." << revision_;
        return ret.str();
    }
    Version(int major, int minor, int revision) {
        major_ = major;
        minor_ = minor;
        revision_ = revision;
    }
    Version(Version& other) {
        major_ = other.major_;
        minor_ = other.minor_;
        revision_ = other.revision_;
    }
    ~Version() {}
    static V8_CALLBACK(NewVersion, 3) {
        if (!args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsNumber())
            throw Exception::TypeError(String::New("You must provide integers!"));
        int arg0 = args[0]->Int32Value();
        int arg1 = args[1]->Int32Value();
        int arg2 = args[2]->Int32Value();
        
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
        if (!value->IsNumber()) throw Exception::TypeError(String::New("You must provide an integer!"));
        h.major_ = value->Int32Value();
    } V8_WRAP_END_NR()
    static V8_SETTER(SetMinor) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        if (!value->IsNumber()) throw Exception::TypeError(String::New("You must provide an integer!"));
        h.minor_ = value->Int32Value();
    } V8_WRAP_END_NR()
    static V8_SETTER(SetRevision) {
        Version& h = *(Unwrap<Version>(info.Holder()));
        if (!value->IsNumber()) throw Exception::TypeError(String::New("You must provide an integer!"));
        h.revision_ = value->Int32Value();
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

extern "C" {
  void initRobotskirt(Handle<Object> target) {
    HandleScope scope;

    //Markdown version class
    Local<FunctionTemplate> verL = FunctionTemplate::New(&Version::NewVersion);
    Persistent<FunctionTemplate> prot = Persistent<FunctionTemplate>::New(verL);
    prot->InstanceTemplate()->SetInternalFieldCount(1);
    prot->SetClassName(String::NewSymbol("Version"));

    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("major"), Version::GetMajor, Version::SetMajor);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("minor"), Version::GetMinor, Version::SetMinor);
    prot->InstanceTemplate()->SetAccessor(String::NewSymbol("revision"), Version::GetRevision, Version::SetRevision);

    NODE_SET_PROTOTYPE_METHOD(prot, "toString", Version::ToString);
    target->Set(String::NewSymbol("Version"), prot->GetFunction());

    //Markdown version function
    Local<FunctionTemplate> mv = FunctionTemplate::New(MarkdownVersion, prot->GetFunction());
    target->Set(String::NewSymbol("markdownVersion"), mv->GetFunction());
    
    //Static functions & properties
    target->Set(String::NewSymbol("version"), newVersionInstance(prot->GetFunction(), 1,0,0 ));
//    NODE_SET_METHOD(target, "markdown", markdown);
//    NODE_SET_METHOD(target, "markdownSync", markdownSync);

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
  }
  NODE_MODULE(robotskirt, initRobotskirt)
}
}
