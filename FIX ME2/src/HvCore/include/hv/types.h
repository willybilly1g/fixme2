#pragma once
#include <stdint.h>
#include <stddef.h>

namespace hv {
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

constexpr u64 page_4k = 4096ULL;
constexpr u64 page_2m = 2ULL * 1024ULL * 1024ULL;
constexpr u64 page_1g = 1024ULL * 1024ULL * 1024ULL;
constexpr u64 page_mask_52 = 0x000FFFFFFFFFF000ULL;

enum class Status : u32 {
    Ok, BadArgument, BadAlignment, BadAddressWidth, Unsupported,
    NoMemory, AlreadyMapped, NotMapped, RequiresSplit, CorruptTable,
    Busy, RollbackFailed, SynchronizationRequired, HandlerMissing
};

// No STL, exceptions, or hidden ownership. All externally provided pointers
// must remain valid for the lifetime of their owning subsystem.
struct MemoryAccess {
    bool read;
    bool write;
    bool execute;
    u8 ept_memory_type; // Only Intel EPT: 0=UC, 6=WB; choose to match physical memory.
    bool npt_write_through;
    bool npt_cache_disable;
    bool npt_pat;
};

inline constexpr MemoryAccess normal_ram() noexcept {
    return {true, true, true, 6, false, false, false};
}

inline constexpr bool aligned(u64 value, u64 alignment) noexcept {
    return (value & (alignment - 1)) == 0;
}

const char* status_name(Status status) noexcept;
} // namespace hv
