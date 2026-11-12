#include "ourcute/geometry.h"
#include <cmath>
namespace ourcute {
bool world_to_screen(Vec3 p, const Matrix4* matrix, int width, int height, Vec2* out) noexcept {
    if (!matrix || !out || width <= 0 || height <= 0) return false;
    const float* m = matrix->m;
    const float x = p.x*m[0] + p.y*m[1] + p.z*m[2] + m[3];
    const float y = p.x*m[4] + p.y*m[5] + p.z*m[6] + m[7];
    const float w = p.x*m[12] + p.y*m[13] + p.z*m[14] + m[15];
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || w < 0.1f) return false;
    const float ndc_x = x / w, ndc_y = y / w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) return false;
    out->x = width * (ndc_x * 0.5f + 0.5f);
    out->y = height * (0.5f - ndc_y * 0.5f);
    return std::isfinite(out->x) && std::isfinite(out->y);
}

bool make_box(Vec3 origin, Vec3 eye, const Matrix4* matrix, int width, int height, Box* out) noexcept {
    if (!out || !matrix) return false;
    Vec2 feet{}, head{};
    if (!world_to_screen(origin, matrix, width, height, &feet) ||
        !world_to_screen(eye, matrix, width, height, &head)) return false;
    const float h = feet.y - head.y;
    if (!std::isfinite(h) || h <= 8.0f || h > height * 4.0f) return false;
    const float w = h / 2.35f;
    const float mid_x = (feet.x + head.x) * 0.5f;
    out->left = mid_x - w*0.5f;
    out->right = mid_x + w*0.5f;
    out->top = head.y;
    out->bottom = feet.y;
    if (out->right < 0 || out->left > width || out->bottom < 0 || out->top > height) return false;
    float scale = h / 60.0f;
    if (scale < 0.6f) scale = 0.6f;
    if (scale > 1.8f) scale = 1.8f;
    out->scale = scale;
    const float a = 6.0f*scale, b = w*0.4f;
    out->corner_length = a < b ? a : b;
    return true;
}
}
