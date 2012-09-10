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

#ifndef V8U_VERSION_HPP
#define	V8U_VERSION_HPP

#include <sstream>

#include "v8u.hpp"

namespace v8u {

class Version: public node::ObjectWrap {
public:
  V8_CL_WRAPPER("v8u::Version")
  Version(int major, int minor, int revision): major_(major), minor_(minor),
                                               revision_(revision) {}
  Version(Version& other): major_(other.major_), minor_(other.minor_),
                           revision_(other.revision_) {}
  ~Version() {}
  V8_CL_CTOR(Version) {
    CheckArguments(3, args);
    int arg0 = Int(args[0]);
    int arg1 = Int(args[1]);
    int arg2 = Int(args[2]);

    inst = new Version(arg0, arg1, arg2);
  } V8_CL_CTOR_END()

  int getMajor() const {return major_;}
  int getMinor() const {return minor_;}
  int getRevision() const {return revision_;}

  void setMajor(int major) {major_=major;}
  void setMinor(int minor) {minor_=minor;}
  void setRevision(int revision) {revision_=revision;}

  std::string toString() const {
    std::stringstream ret;
    ret << major_ << "." << minor_ << "." << revision_;
    return ret.str();
  }

  V8_CL_CALLBACK(Version, ToString) {
    return scope.Close(Str(inst->toString()));
  } V8_CALLBACK_END()

  V8_CL_CALLBACK(Version, Inspect) {
    return scope.Close(Str("<Version "+inst->toString()+">"));
  } V8_CALLBACK_END()

  //Getters
  V8_CL_GETTER(Version, Major) {
    return scope.Close(Int(inst->major_));
  } V8_GETTER_END()
  V8_CL_GETTER(Version, Minor) {
    return scope.Close(Int(inst->minor_));
  } V8_GETTER_END()
  V8_CL_GETTER(Version, Revision) {
    return scope.Close(Int(inst->revision_));
  } V8_GETTER_END()

  //Setters
  V8_CL_SETTER(Version, Major) {
    inst->major_ = Int(value);
  } V8_SETTER_END()
  V8_CL_SETTER(Version, Minor) {
    inst->minor_ = Int(value);
  } V8_SETTER_END()
  V8_CL_SETTER(Version, Revision) {
    inst->revision_ = Int(value);
  } V8_SETTER_END()

  NODE_DEF_TYPE("Version") {
    V8_DEF_PROP(Major, "major");
    V8_DEF_PROP(Minor, "minor");
    V8_DEF_PROP(Revision, "revision");

    V8_DEF_METHOD(ToString, "toString");
    V8_DEF_METHOD(Inspect, "inspect");

    StoreTemplate("v8u::Version", prot);
  } NODE_DEF_TYPE_END()
private:
  int major_, minor_, revision_;
};

};

#endif	/* V8U_VERSION_HPP */

