#pragma once
#include <stdint.h>
namespace ourcute {
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Matrix4 { float m[16]; };
struct Box { float left, top, right, bottom, scale, corner_length; };
bool world_to_screen(Vec3 point, const Matrix4* matrix, int width, int height, Vec2* out) noexcept;
bool make_box(Vec3 origin, Vec3 eye, const Matrix4* matrix, int width, int height, Box* out) noexcept;
}
