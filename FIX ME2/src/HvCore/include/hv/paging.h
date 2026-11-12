#pragma once
#include "hv/types.h"

namespace hv {

enum class Backend : u8 { IntelEpt, AmdNpt };

struct TablePage {
    u64 physical_address;
    u64* entries; // 4096-byte aligned, writable, exactly 512 u64 entries
};

using AllocateTable = bool (*)(void* user, TablePage* out) noexcept;
using ResolveTable = bool (*)(void* user, u64 physical_address, TablePage* out) noexcept;
using ReleaseTable = void (*)(void* user, TablePage table) noexcept;

struct PageProvider {
    void* user;
    AllocateTable allocate;
    ResolveTable resolve;
    ReleaseTable release;
};

struct AddressSpace {
    Backend backend;
    PageProvider provider;
    TablePage root;
    u64 generation;
    u8 max_physical_address_bits;
    bool initialized;
    // Deliberately OFFLINE-only. No hardware activation until TLB/IPI/CPU
    // rendezvous are implemented and independently validated.
};

struct Translation {
    u64 host_physical_address;
    u64 mapping_base;
    u64 page_size;
    MemoryAccess access;
};

// Callers MUST serialize all writers and must not permit a hardware vCPU to
// walk these tables. APIs below implement offline construction/testing only.
Status space_init(AddressSpace* space, Backend backend,
                  PageProvider provider, u8 max_phys_bits) noexcept;
Status space_destroy(AddressSpace* space) noexcept;
Status map_2m(AddressSpace* space, u64 guest_pa, u64 host_pa,
              MemoryAccess access) noexcept;
Status map_4k(AddressSpace* space, u64 guest_pa, u64 host_pa,
              MemoryAccess access) noexcept;
Status split_2m(AddressSpace* space, u64 guest_pa) noexcept;
Status protect_4k(AddressSpace* space, u64 guest_pa,
                  MemoryAccess new_access) noexcept;
Status unmap_4k(AddressSpace* space, u64 guest_pa) noexcept;
Status translate(const AddressSpace* space, u64 guest_pa,
                 Translation* out) noexcept;

// These helpers expose bits for independent tests; they never touch CPUs.
Status encode_leaf(Backend backend, u64 physical_address, u64 page_size,
                   MemoryAccess access, u64* out_entry) noexcept;
Status decode_leaf(Backend backend, u64 entry, u64 page_size,
                   MemoryAccess* out_access) noexcept;

} // namespace hv
