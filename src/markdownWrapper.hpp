#ifndef MARKDOWNHELPER_H
#define	MARKDOWNHELPER_H

extern "C" {
  #include"markdown.h"
  #include"html.h"
}

namespace mkd {

//    class Renderer {
//    protected:
//        mkd_renderer ptr;
//    public:
//        inline Renderer() {}
//        inline ~Renderer() {}
//        inline mkd_renderer& getRenderer() {return ptr;}
//    };
//
//    class HtmlRenderer: public Renderer {
//    public:
//        inline HtmlRenderer(unsigned int render_flags) {upshtml_renderer(&ptr, render_flags);}
//        inline ~HtmlRenderer() {upshtml_free_renderer(&ptr);}
//    };
//
//    class TocHtmlRenderer: public Renderer {
//    public:
//        inline TocHtmlRenderer() {upshtml_toc_renderer(&ptr);}
//        inline ~TocHtmlRenderer() {upshtml_free_renderer(&ptr);}
//    };

};

#endif
