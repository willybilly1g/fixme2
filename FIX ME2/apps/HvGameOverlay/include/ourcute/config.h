#pragma once
#include <stddef.h>
#include <stdint.h>
// Identifier macros requested by the caller. ENC is a compatibility macro only;
// this project does not use string-obfuscation or stealth techniques.
#ifndef ENC
#define ENC(text) (text)
#endif
#define ENC_OURCUTE_EXE        ENC(L"ourcutegame.exe")
#define ENC_CLIENT_DLL         ENC(L"ourcutegame.dll")
#define ENC_GAME_WINDOW_TITLE  ENC(L"Our Cute Game")

namespace ourcute::offsets {
// Exact values from user-provided OurCuteGame2/src/FeetPics/offsets_local.h.
// They are version-dependent and MUST be revalidated against the target build.
constexpr uintptr_t entity_list = 0x271FBB8ull;
constexpr uintptr_t view_matrix = 0x2570250ull;
constexpr uintptr_t local_player_pawn = 0x256A948ull;
constexpr uintptr_t pawn_handle = 0x92Cull;
constexpr uintptr_t origin = 0x14A4ull;
constexpr uintptr_t view_offset = 0xF60ull;
constexpr uintptr_t health = 0x34Cull;
constexpr uintptr_t team = 0x3E7ull;
constexpr uintptr_t scene_node = 0x330ull;
constexpr uintptr_t dormant = 0x103ull;
constexpr uintptr_t entity_list_header = 0x10ull;
constexpr uintptr_t entity_stride = 0x70ull;
}
