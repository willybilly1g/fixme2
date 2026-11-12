#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include "ourcute/geometry.h"

namespace ourcute {
constexpr unsigned max_entities = 128;
// Fixed-size DOD snapshot, single writer / many reader semantics via SRWLOCK.
struct Snapshot {
    Matrix4 view;
    Vec3 origin[max_entities];
    Vec3 eye[max_entities];
    int health[max_entities];
    uint32_t pid;
    uintptr_t module_base;
    ULONGLONG collected_at_ms;
    unsigned count;
    unsigned scanned;
    unsigned rejected;
    bool attached;
    bool matrix_valid;
};
void game_start() noexcept;
void game_stop() noexcept;
void game_snapshot(Snapshot* output) noexcept;
}
