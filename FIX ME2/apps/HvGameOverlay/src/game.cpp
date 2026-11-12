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
#include "hv/process.h"
#include "hv/introspection_win32.h"
#include "ourcute/config.h"
#include "ourcute/game.h"

namespace ourcute {
namespace {
SRWLOCK snapshot_lock = SRWLOCK_INIT;
Snapshot shared = {};
HANDLE worker_handle = nullptr;
volatile LONG stop_requested = 0;

bool checked_add(uintptr_t a, uintptr_t b, uintptr_t* result) noexcept {
    if (!result || b > UINTPTR_MAX - a) return false;
    *result = a + b;
    return true;
}

template <typename T> bool read_at(const hv::introspection::Context* channel, uintptr_t address, T* output) noexcept {
    if (!output || !address) return false;
    T temp = {};
    uint8_t scratch[sizeof(T)] = {};
    const hv::introspection::Request request = {static_cast<uint64_t>(address), &temp, static_cast<uint32_t>(sizeof(T))};
    if (hv::introspection::read_batch(channel, &request, 1, scratch, sizeof(scratch)) !=
        hv::introspection::Status::Ok) return false;
    *output = temp;
    return true;
}
template <typename T> bool read_offset(const hv::introspection::Context* channel, uintptr_t base, uintptr_t offset, T* out) noexcept {
    uintptr_t address = 0;
    return checked_add(base, offset, &address) && read_at(channel, address, out);
}

// The game window is the source of truth; never attach to an unrelated same-named process.
bool select_target(hv::process::Target* target, uintptr_t* module_base) noexcept {
    HWND window = FindWindowW(nullptr, ENC_GAME_WINDOW_TITLE);
    DWORD pid = 0;
    if (!window || !GetWindowThreadProcessId(window, &pid) || !pid) return false;
    const auto opened = hv::process::attach(target, pid);
    if (opened.code != hv::process::Code::Ok) return false;
    wchar_t name[260] = {};
    if (hv::process::get_target_name(target, name, 260).code != hv::process::Code::Ok ||
        lstrcmpiW(name, ENC_OURCUTE_EXE) != 0) {
        hv::process::detach(target);
        return false;
    }
    hv::process::Module client = {};
    if (hv::process::find_module(target, ENC_CLIENT_DLL, &client).code != hv::process::Code::Ok ||
        client.base == 0 || client.size < 4096) {
        hv::process::detach(target);
        return false;
    }
    *module_base = client.base;
    return true;
}

// Read one coherent best-effort snapshot. Failures never retain old player boxes.
void sample(const hv::process::Target* target, const hv::introspection::Context* channel,
            uintptr_t client_base, Snapshot* state) noexcept {
    *state = {};
    state->attached = true;
    state->pid = target->pid;
    state->module_base = client_base;
    Matrix4 matrix = {};
    uintptr_t list = 0, pawn = 0;
    uintptr_t matrix_va = 0, list_va = 0, pawn_va = 0;
    if (!checked_add(client_base, offsets::view_matrix, &matrix_va) ||
        !checked_add(client_base, offsets::entity_list, &list_va) ||
        !checked_add(client_base, offsets::local_player_pawn, &pawn_va)) return;
    // One bounded staged transaction: no partial destination updates if a
    // field is unreadable. Windows does not promise an atomic process snapshot.
    const hv::introspection::Request initial[3] = {
        {static_cast<uint64_t>(matrix_va), &matrix, static_cast<uint32_t>(sizeof(matrix))},
        {static_cast<uint64_t>(list_va), &list, static_cast<uint32_t>(sizeof(list))},
        {static_cast<uint64_t>(pawn_va), &pawn, static_cast<uint32_t>(sizeof(pawn))}
    };
    uint8_t staging[sizeof(matrix) + sizeof(list) + sizeof(pawn)] = {};
    if (hv::introspection::read_batch(channel, initial, 3, staging, sizeof(staging)) !=
        hv::introspection::Status::Ok || !list || !pawn) return;
    state->matrix_valid = true;
    state->view = matrix;
    uint8_t local_team = 0;
    if (!read_offset(channel, pawn, offsets::team, &local_team)) return;

    // Cache list entries per 512-entity chunk. This version scans slots 1..127.
    uintptr_t head = 0;
    if (!read_offset(channel, list, offsets::entity_list_header, &head) || !head) return;
    for (unsigned i = 1; i < max_entities; ++i) {
        ++state->scanned;
        uintptr_t controller = 0;
        if (!read_offset(channel, head, uintptr_t(i)*offsets::entity_stride, &controller) || !controller) continue;
        uint32_t handle = 0;
        if (!read_offset(channel, controller, offsets::pawn_handle, &handle) || !handle || handle == UINT32_MAX) continue;
        const unsigned index = handle & 0x7FFFu;
        uintptr_t pawn_chunk = 0;
        if (!read_offset(channel, list, offsets::entity_list_header + uintptr_t(index >> 9)*8, &pawn_chunk) || !pawn_chunk) continue;
        uintptr_t entity = 0;
        if (!read_offset(channel, pawn_chunk, uintptr_t(index & 0x1FFu)*offsets::entity_stride, &entity) || !entity || entity == pawn) continue;
        uintptr_t scene = 0;
        uint8_t dormant = 0, team = 0;
        int health = 0;
        Vec3 origin = {}, view_off = {};
        if (!read_offset(channel, entity, offsets::scene_node, &scene) || !scene ||
            !read_offset(channel, scene, offsets::dormant, &dormant) || dormant ||
            !read_offset(channel, entity, offsets::team, &team) || !team || team == local_team ||
            !read_offset(channel, entity, offsets::health, &health) || health <= 0 || health > 100 ||
            !read_offset(channel, entity, offsets::origin, &origin) ||
            !read_offset(channel, entity, offsets::view_offset, &view_off)) { ++state->rejected; continue; }
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
            !std::isfinite(view_off.x) || !std::isfinite(view_off.y) || !std::isfinite(view_off.z) ||
            view_off.z < 1.0f || view_off.z > 128.0f) { ++state->rejected; continue; }
        if (state->count >= max_entities) break;
        unsigned out = state->count++;
        state->origin[out] = origin;
        state->eye[out] = {origin.x+view_off.x, origin.y+view_off.y, origin.z+view_off.z};
        state->health[out] = health;
    }
    state->collected_at_ms = GetTickCount64();
}

DWORD WINAPI worker(void*) noexcept {
    hv::process::Target target = {};
    uintptr_t base = 0;
    hv::introspection::Context channel = {};
    ULONGLONG next_module_check = 0;
    while (InterlockedCompareExchange(&stop_requested, 0, 0) == 0) {
        if (!hv::process::is_attached(&target)) {
            if (!select_target(&target, &base) ||
                hv::process::bind_introspection(&target, &channel) != hv::introspection::Status::Ok) {
                hv::process::detach(&target);
                AcquireSRWLockExclusive(&snapshot_lock); shared = {}; ReleaseSRWLockExclusive(&snapshot_lock);
                Sleep(500); continue;
            }
        }
        bool alive = false;
        HWND window = FindWindowW(nullptr, ENC_GAME_WINDOW_TITLE);
        DWORD window_pid = 0;
        if (window) GetWindowThreadProcessId(window, &window_pid);
        if (hv::process::is_alive(&target, &alive).code != hv::process::Code::Ok || !alive ||
            window_pid != target.pid) {
            hv::process::detach(&target); base = 0; next_module_check = 0;
            AcquireSRWLockExclusive(&snapshot_lock); shared = {}; ReleaseSRWLockExclusive(&snapshot_lock);
            Sleep(250); continue;
        }
        if (GetTickCount64() >= next_module_check) {
            hv::process::Module current = {};
            if (hv::process::find_module(&target, ENC_CLIENT_DLL, &current).code != hv::process::Code::Ok ||
                !current.base) {
                hv::process::detach(&target); base = 0;
                AcquireSRWLockExclusive(&snapshot_lock); shared = {}; ReleaseSRWLockExclusive(&snapshot_lock);
                Sleep(500); continue;
            }
            base = current.base;
            next_module_check = GetTickCount64() + 2000;
        }
        Snapshot next = {};
        sample(&target, &channel, base, &next);
        AcquireSRWLockExclusive(&snapshot_lock);
        shared = next;
        ReleaseSRWLockExclusive(&snapshot_lock);
        Sleep(30);
    }
    hv::process::detach(&target);
    AcquireSRWLockExclusive(&snapshot_lock); shared = {}; ReleaseSRWLockExclusive(&snapshot_lock);
    return 0;
}
}

void game_start() noexcept {
    if (worker_handle) return;
    InterlockedExchange(&stop_requested, 0);
    worker_handle = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
}
void game_stop() noexcept {
    InterlockedExchange(&stop_requested, 1);
    if (worker_handle) { WaitForSingleObject(worker_handle, INFINITE); CloseHandle(worker_handle); worker_handle = nullptr; }
}
void game_snapshot(Snapshot* output) noexcept {
    if (!output) return;
    AcquireSRWLockShared(&snapshot_lock); *output = shared; ReleaseSRWLockShared(&snapshot_lock);
}
}
