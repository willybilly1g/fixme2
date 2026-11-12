#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "ourcute/game.h"
#include "hv/kernel_protocol.h"
namespace ourcute {
struct Layer {
    HWND window;
    HDC memory_dc;
    HBITMAP bitmap;
    HBITMAP previous_bitmap;
    HBRUSH key_brush;
    HBRUSH dark_brush;
    HBRUSH gradient[256];
    int width, height;
};
constexpr COLORREF transparency_key = RGB(1, 2, 3);
bool layer_init(Layer* layer, HWND window, int width, int height) noexcept;
bool layer_resize(Layer* layer, int width, int height) noexcept;
void layer_destroy(Layer* layer) noexcept;
void layer_draw(Layer* layer, const Snapshot* snapshot, bool demo) noexcept;
void layer_draw_kernel_test(Layer* layer, int x, int y, int width, int height, int health) noexcept;
void layer_draw_frame(Layer* layer,const hv::kernel_protocol::FrameReply* frame) noexcept;
void layer_present(Layer* layer) noexcept;
}
