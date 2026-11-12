#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <cmath>
#include "ourcute/render.h"

namespace ourcute {
namespace {
void draw_line(HDC dc, float x1, float y1, float x2, float y2) noexcept {
    MoveToEx(dc, static_cast<int>(x1), static_cast<int>(y1), nullptr);
    LineTo(dc, static_cast<int>(x2), static_cast<int>(y2));
}
void paint_box(Layer* l, const Box* b, int health) noexcept {
    const float x1=b->left, y1=b->top, x2=b->right, y2=b->bottom;
    const float c=b->corner_length;
    HPEN green=CreatePen(PS_SOLID, 1, RGB(0,255,0));
    if (!green) return;
    HGDIOBJ previous=SelectObject(l->memory_dc,green);
    draw_line(l->memory_dc,x1,y1,x1+c,y1);
    draw_line(l->memory_dc,x1,y1,x1,y1+c);
    draw_line(l->memory_dc,x2-c,y1,x2,y1);
    draw_line(l->memory_dc,x2,y1,x2,y1+c);
    draw_line(l->memory_dc,x1,y2-c,x1,y2);
    draw_line(l->memory_dc,x1,y2,x1+c,y2);
    draw_line(l->memory_dc,x2-c,y2,x2,y2);
    draw_line(l->memory_dc,x2,y2-c,x2,y2);
    SelectObject(l->memory_dc,previous);
    DeleteObject(green);
    HPEN white=CreatePen(PS_SOLID,1,RGB(255,255,255));
    if(white) {
        previous=SelectObject(l->memory_dc,white);
        draw_line(l->memory_dc,static_cast<float>(l->width)*0.5f,static_cast<float>(l->height), (x1+x2)*0.5f,y2);
        SelectObject(l->memory_dc,previous);
        DeleteObject(white);
    }
    const float bw=3.5f*b->scale, bx=x1-bw-5.0f*b->scale;
    const float h=y2-y1;
    RECT background={static_cast<LONG>(bx),static_cast<LONG>(y1),static_cast<LONG>(bx+bw+1),static_cast<LONG>(y2)};
    FillRect(l->memory_dc,&background,l->dark_brush);
    float ratio=static_cast<float>(health)/100.0f;
    if(ratio<0)ratio=0;
    if(ratio>1)ratio=1;
    int top=static_cast<int>(y1+h*(1.0f-ratio));
    int bottom=static_cast<int>(y2);
    // Preserve original gradient scheme but reuse 256 prebuilt brushes.
    if(top<0)top=0;
    if(bottom>l->height)bottom=l->height;
    for(int row=top;row<bottom;++row) {
        float t=(static_cast<float>(row)-y1)/h;
        if(t<0)t=0;
        if(t>1)t=1;
        const unsigned index=static_cast<unsigned>(t*255.0f);
        RECT r={static_cast<LONG>(bx),row,static_cast<LONG>(bx+bw+1),row+1};
        FillRect(l->memory_dc,&r,l->gradient[index]);
    }
}
}

bool layer_resize(Layer* l,int w,int h) noexcept {
    if(!l||!l->memory_dc||w<1||h<1||w>16384||h>16384)return false;
    if(l->bitmap && w==l->width && h==l->height)return true;
    HDC dc=GetDC(l->window);
    if(!dc)return false;
    HBITMAP replacement=CreateCompatibleBitmap(dc,w,h);
    ReleaseDC(l->window,dc);
    if(!replacement)return false;
    HGDIOBJ old=SelectObject(l->memory_dc,replacement);
    if(!old||old==HGDI_ERROR){DeleteObject(replacement);return false;}
    if(!l->previous_bitmap)l->previous_bitmap=static_cast<HBITMAP>(old);
    if(l->bitmap)DeleteObject(l->bitmap);
    l->bitmap=replacement;l->width=w;l->height=h;
    return true;
}
bool layer_init(Layer* l,HWND window,int w,int h) noexcept {
    if(!l||!window)return false;
    *l={};l->window=window;
    HDC dc=GetDC(window);
    if(!dc)return false;
    l->memory_dc=CreateCompatibleDC(dc);
    ReleaseDC(window,dc);
    if(!l->memory_dc)return false;
    l->key_brush=CreateSolidBrush(transparency_key);
    l->dark_brush=CreateSolidBrush(RGB(25,25,25));
    if(!l->key_brush||!l->dark_brush){layer_destroy(l);return false;}
    for(unsigned i=0;i<256;++i) {
        const float t=static_cast<float>(i)/255.0f;
        const int r=t<0.5f?static_cast<int>(255.0f*t*2.0f):255;
        const int g=t<0.5f?255:static_cast<int>(255.0f*(1.0f-(t-0.5f)*2.0f));
        l->gradient[i]=CreateSolidBrush(RGB(r,g,40));
        if(!l->gradient[i]){layer_destroy(l);return false;}
    }
    if(!layer_resize(l,w,h)){layer_destroy(l);return false;}
    return true;
}
void layer_destroy(Layer* l) noexcept {
    if(!l)return;
    if(l->memory_dc && l->previous_bitmap)SelectObject(l->memory_dc,l->previous_bitmap);
    if(l->bitmap)DeleteObject(l->bitmap);
    if(l->memory_dc)DeleteDC(l->memory_dc);
    if(l->key_brush)DeleteObject(l->key_brush);
    if(l->dark_brush)DeleteObject(l->dark_brush);
    for(unsigned i=0;i<256;++i)if(l->gradient[i])DeleteObject(l->gradient[i]);
    *l={};
}
void layer_draw(Layer* l,const Snapshot* s,bool demo) noexcept {
    if(!l||!l->memory_dc||!s)return;
    RECT full={0,0,l->width,l->height};
    FillRect(l->memory_dc,&full,l->key_brush);
    if(!demo && (!s->attached || !s->matrix_valid || !s->collected_at_ms || GetTickCount64()-s->collected_at_ms>300))return;
    for(unsigned i=0;i<s->count && i<max_entities;++i) {
        Box box={};
        if(make_box(s->origin[i],s->eye[i],&s->view,l->width,l->height,&box))
            paint_box(l,&box,s->health[i]);
    }
}
// Draw one kernel-generated synthetic sample through the SAME original box painter.
// This is transport validation; it is never sourced from a game's memory.
void layer_draw_kernel_test(Layer* l,int x,int y,int width,int height,int health) noexcept {
    if(!l || !l->memory_dc || width<8 || height<8 || x<0 || y<0 ||
       x+width>l->width || y+height>l->height) return;
    RECT full={0,0,l->width,l->height};
    FillRect(l->memory_dc,&full,l->key_brush);
    Box b={static_cast<float>(x),static_cast<float>(y),
           static_cast<float>(x+width),static_cast<float>(y+height),1.0f,16.0f};
    paint_box(l,&b,health);
}
// This pipeline only renders producer-provided and validated rectangles.
// It does not infer object positions or read an external game's memory.
void layer_draw_frame(Layer* l,const hv::kernel_protocol::FrameReply* reply) noexcept {
    if(!l || !l->memory_dc) return;
    RECT full={0,0,l->width,l->height};
    FillRect(l->memory_dc,&full,l->key_brush);
    if(!reply || reply->code!=static_cast<uint32_t>(hv::kernel_protocol::Code::Ok) ||
       !hv::kernel_protocol::validate_payload(&reply->payload)) return;
    const hv::kernel_protocol::FramePayload& frame=reply->payload;
    const float scale_x=static_cast<float>(l->width)/frame.viewport_width;
    const float scale_y=static_cast<float>(l->height)/frame.viewport_height;
    for(uint32_t i=0;i<frame.count;i++) {
        const hv::kernel_protocol::FrameBox& b=frame.boxes[i];
        const float x=static_cast<float>(b.x)*scale_x;
        const float y=static_cast<float>(b.y)*scale_y;
        const float w=static_cast<float>(b.width)*scale_x;
        const float h=static_cast<float>(b.height)*scale_y;
        if(w<8.0f || h<8.0f) continue;
        Box projected={x,y,x+w,y+h,1.0f,16.0f};
        paint_box(l,&projected,static_cast<int>(b.health));
    }
}
void layer_present(Layer* l) noexcept {
    if(!l||!l->window||!l->memory_dc)return;
    HDC dc=GetDC(l->window);
    if(dc){BitBlt(dc,0,0,l->width,l->height,l->memory_dc,0,0,SRCCOPY);ReleaseDC(l->window,dc);}
}
}
