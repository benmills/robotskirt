#ifndef MARKDOWNHELPER_H
#define	MARKDOWNHELPER_H

extern "C" {
  #include"markdown.h"
  #include"html.h"
}

namespace mkd {

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

};

#endif
