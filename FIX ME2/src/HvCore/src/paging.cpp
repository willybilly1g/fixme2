#include "hv/paging.h"

namespace hv {
namespace {
constexpr u64 bit(u32 n) noexcept { return 1ULL << n; }
constexpr u64 phys_4k = page_mask_52;
constexpr u64 phys_2m = page_mask_52 & ~(page_2m - 1ULL);

bool is_present(Backend b, u64 e) noexcept {
    return b == Backend::IntelEpt ? (e & 7ULL) != 0 : (e & 1ULL) != 0;
}

bool valid_physical(u64 pa, u8 bits, u64 alignment) noexcept {
    if (bits < 36 || bits > 52 || !aligned(pa, alignment)) return false;
    return (pa >> bits) == 0;
}

bool valid_gpa(u64 gpa, u64 alignment) noexcept {
    return aligned(gpa, alignment) && (gpa >> 48) == 0;
}

u32 index_for(u64 gpa, u32 level) noexcept {
    constexpr u32 shifts[4] = {39, 30, 21, 12};
    return static_cast<u32>((gpa >> shifts[level]) & 511ULL);
}

bool valid_attributes(Backend b, MemoryAccess a) noexcept {
    if (!a.read || (a.write && !a.read)) return false;
    if (b == Backend::IntelEpt) {
        // We deliberately support only UC/WB, do not use unknown memory types.
        if (a.ept_memory_type != 0 && a.ept_memory_type != 6) return false;
        if (a.npt_write_through || a.npt_cache_disable || a.npt_pat) return false;
    } else if (a.ept_memory_type != 0 && a.ept_memory_type != 6) {
        return false;
    }
    return true;
}

u64 table_entry(Backend b, u64 pa) noexcept {
    return pa | (b == Backend::IntelEpt ? 7ULL : 7ULL);
}

Status resolve(const AddressSpace* s, u64 pa, TablePage* page) noexcept {
    if (!s->provider.resolve(s->provider.user, pa, page)) return Status::CorruptTable;
    if (!page->entries || page->physical_address != pa ||
        !aligned(pa, page_4k) ||
        !aligned(static_cast<u64>(reinterpret_cast<uintptr_t>(page->entries)), page_4k) ||
        !valid_physical(pa, s->max_physical_address_bits, page_4k)) return Status::CorruptTable;
    return Status::Ok;
}

Status allocate(const AddressSpace* s, TablePage* page) noexcept {
    if (!s->provider.allocate(s->provider.user, page)) return Status::NoMemory;
    if (!page->entries || !valid_physical(page->physical_address,
            s->max_physical_address_bits, page_4k) ||
        !aligned(static_cast<u64>(reinterpret_cast<uintptr_t>(page->entries)), page_4k)) {
        s->provider.release(s->provider.user, *page);
        return Status::BadAlignment;
    }
    for (u32 i = 0; i < 512; ++i) page->entries[i] = 0;
    return Status::Ok;
}

// Returns the PD entry (depth=3) or PT entry (depth=4). No partial
// publication on allocation failure: allocate+link private chain first,
// commit exactly one pointer into the existing tree.
Status leaf_slot(AddressSpace* s, u64 gpa, u32 depth, bool create, u64** out) noexcept {
    if (!s || !s->initialized || !out || (depth != 3 && depth != 4))
        return Status::BadArgument;
    TablePage current = s->root;
    for (u32 level = 0; level < depth - 1; ++level) {
        u64* e = &current.entries[index_for(gpa, level)];
        if (is_present(s->backend, *e)) {
            if ((*e & bit(7)) && level >= 1) return Status::RequiresSplit;
            const u64 child = *e & phys_4k;
            Status result = resolve(s, child, &current);
            if (result != Status::Ok) return result;
            continue;
        }
        if (*e != 0) return Status::CorruptTable;
        if (!create) return Status::NotMapped;

        const u32 needed = depth - 1 - level;
        TablePage staged[3] = {};
        for (u32 i = 0; i < needed; ++i) {
            Status result = allocate(s, &staged[i]);
            if (result != Status::Ok) {
                for (u32 j = 0; j < i; ++j) s->provider.release(s->provider.user, staged[j]);
                return result;
            }
        }
        for (u32 i = 0; i + 1 < needed; ++i) {
            staged[i].entries[index_for(gpa, level + 1 + i)] =
                table_entry(s->backend, staged[i + 1].physical_address);
        }
        // Publication is safe for an OFFLINE table only. Hardware access
        // requires additional synchronization and translation invalidation.
        *e = table_entry(s->backend, staged[0].physical_address);
        current = staged[needed - 1];
        break;
    }
    *out = &current.entries[index_for(gpa, depth - 1)];
    return Status::Ok;
}

Status checked_leaf_slot(AddressSpace* s, u64 gpa, u32 depth, u64** out) noexcept {
    if (!valid_gpa(gpa, depth == 3 ? page_2m : page_4k)) return Status::BadAlignment;
    return leaf_slot(s, gpa, depth, false, out);
}

// Destroy child page-table structures, NEVER mapped physical data. For use
// only with a quiesced/offline address space; provider owns physical memory.
Status validate_children(const AddressSpace* s, TablePage page, u32 depth) noexcept {
    for (u32 i = 0; i < 512; ++i) {
        const u64 e = page.entries[i];
        if (!is_present(s->backend, e)) {
            if (e != 0) return Status::CorruptTable;
            continue;
        }
        if (depth == 3) {
            MemoryAccess a = {};
            if (decode_leaf(s->backend, e, page_4k, &a) != Status::Ok)
                return Status::CorruptTable;
            continue;
        }
        if (depth >= 1 && (e & bit(7))) {
            if (depth == 1) return Status::Unsupported; // 1 GiB pages out of scope
            MemoryAccess a = {};
            if (decode_leaf(s->backend, e, page_2m, &a) != Status::Ok)
                return Status::CorruptTable;
            continue;
        }
        TablePage child = {};
        Status r = resolve(s, e & phys_4k, &child);
        if (r != Status::Ok) return r;
        r = validate_children(s, child, depth + 1);
        if (r != Status::Ok) return r;
    }
    return Status::Ok;
}

Status free_children(AddressSpace* s, TablePage page, u32 depth) noexcept {
    if (depth >= 4) return Status::Ok;
    Status first_error = Status::Ok;
    for (u32 i = 0; i < 512; ++i) {
        const u64 entry = page.entries[i];
        if (!is_present(s->backend, entry)) {
            if (entry != 0 && first_error == Status::Ok) first_error = Status::CorruptTable;
            continue;
        }
        if ((entry & bit(7)) && depth >= 2) continue;
        if (depth == 3) continue;
        TablePage child = {};
        Status r = resolve(s, entry & phys_4k, &child);
        if (r != Status::Ok) { if (first_error == Status::Ok) first_error = r; continue; }
        r = free_children(s, child, depth + 1);
        if (r != Status::Ok && first_error == Status::Ok) first_error = r;
        s->provider.release(s->provider.user, child);
        page.entries[i] = 0;
    }
    return first_error;
}

Status modify_generation(AddressSpace* s) noexcept {
    if (s->generation == ~0ULL) return Status::Unsupported;
    ++s->generation;
    return Status::Ok;
}
} // namespace

Status encode_leaf(Backend b, u64 pa, u64 size, MemoryAccess a, u64* out) noexcept {
    if (!out || (size != page_4k && size != page_2m)) return Status::BadArgument;
    if (!valid_attributes(b, a)) return Status::BadArgument;
    if (!aligned(pa, size)) return Status::BadAlignment;
    if ((pa >> 52) != 0) return Status::BadAddressWidth;
    u64 e = pa;
    if (b == Backend::IntelEpt) {
        e |= (a.read ? bit(0) : 0) | (a.write ? bit(1) : 0)
           | (a.execute ? bit(2) : 0) | (static_cast<u64>(a.ept_memory_type) << 3);
    } else {
        e |= bit(0) | bit(2) | (a.write ? bit(1) : 0)
           | (a.npt_write_through ? bit(3) : 0)
           | (a.npt_cache_disable ? bit(4) : 0)
           | (!a.execute ? bit(63) : 0)
           | (a.npt_pat ? (size == page_2m ? bit(12) : bit(7)) : 0);
    }
    if (size == page_2m) e |= bit(7);
    *out = e;
    return Status::Ok;
}

Status decode_leaf(Backend b, u64 e, u64 size, MemoryAccess* out) noexcept {
    if (!out || !is_present(b, e) || (size != page_4k && size != page_2m))
        return Status::BadArgument;
    MemoryAccess a = {};
    if (b == Backend::IntelEpt) {
        a.read = (e & bit(0)) != 0;
        a.write = (e & bit(1)) != 0;
        a.execute = (e & bit(2)) != 0;
        a.ept_memory_type = static_cast<u8>((e >> 3) & 7ULL);
    } else {
        a.read = true;
        a.write = (e & bit(1)) != 0;
        a.execute = (e & bit(63)) == 0;
        a.ept_memory_type = 6; // ignored for NPT, keep conventional default
        a.npt_write_through = (e & bit(3)) != 0;
        a.npt_cache_disable = (e & bit(4)) != 0;
        a.npt_pat = (e & (size == page_2m ? bit(12) : bit(7))) != 0;
    }
    if (!valid_attributes(b, a)) return Status::CorruptTable;
    *out = a;
    return Status::Ok;
}

Status space_init(AddressSpace* s, Backend b, PageProvider p, u8 bits) noexcept {
    if (!s || s->initialized || !p.allocate || !p.resolve || !p.release ||
        bits < 36 || bits > 52) return Status::BadArgument;
    AddressSpace tmp = {};
    tmp.backend = b;
    tmp.provider = p;
    tmp.max_physical_address_bits = bits;
    Status result = allocate(&tmp, &tmp.root);
    if (result != Status::Ok) return result;
    tmp.initialized = true;
    *s = tmp;
    return Status::Ok;
}

Status space_destroy(AddressSpace* s) noexcept {
    if (!s || !s->initialized) return Status::BadArgument;
    Status result = validate_children(s, s->root, 0);
    if (result != Status::Ok) return result; // fail before freeing anything
    result = free_children(s, s->root, 0);
    // If a corrupt tree was encountered, don't free the root and leave
    // potential dangling references: let the owner inspect and quarantine.
    if (result != Status::Ok) return result;
    s->provider.release(s->provider.user, s->root);
    *s = {};
    return Status::Ok;
}

Status map_2m(AddressSpace* s, u64 gpa, u64 hpa, MemoryAccess a) noexcept {
    if (!s || !s->initialized) return Status::BadArgument;
    if (!valid_gpa(gpa, page_2m) || !aligned(hpa, page_2m)) return Status::BadAlignment;
    if (!valid_physical(hpa, s->max_physical_address_bits, page_2m) ||
        hpa > ((1ULL << s->max_physical_address_bits) - page_2m)) return Status::BadAddressWidth;
    u64 encoded = 0;
    Status r = encode_leaf(s->backend, hpa, page_2m, a, &encoded);
    if (r != Status::Ok) return r;
    if (s->generation == ~0ULL) return Status::Unsupported;
    u64* e = nullptr;
    r = leaf_slot(s, gpa, 3, true, &e);
    if (r != Status::Ok) return r;
    if (*e != 0) return Status::AlreadyMapped;
    r = modify_generation(s);
    if (r != Status::Ok) return r;
    *e = encoded;
    return Status::Ok;
}

Status map_4k(AddressSpace* s, u64 gpa, u64 hpa, MemoryAccess a) noexcept {
    if (!s || !s->initialized) return Status::BadArgument;
    if (!valid_gpa(gpa, page_4k) || !aligned(hpa, page_4k)) return Status::BadAlignment;
    if (!valid_physical(hpa, s->max_physical_address_bits, page_4k)) return Status::BadAddressWidth;
    u64 encoded = 0;
    Status r = encode_leaf(s->backend, hpa, page_4k, a, &encoded);
    if (r != Status::Ok) return r;
    if (s->generation == ~0ULL) return Status::Unsupported;
    u64* e = nullptr;
    r = leaf_slot(s, gpa, 4, true, &e);
    if (r != Status::Ok) return r;
    if (*e != 0) return Status::AlreadyMapped;
    r = modify_generation(s);
    if (r != Status::Ok) return r;
    *e = encoded;
    return Status::Ok;
}

Status split_2m(AddressSpace* s, u64 gpa) noexcept {
    if (!s || !s->initialized) return Status::BadArgument;
    u64* parent = nullptr;
    Status r = checked_leaf_slot(s, gpa, 3, &parent);
    if (r != Status::Ok) return r;
    const u64 old = *parent;
    if (!is_present(s->backend, old)) return Status::NotMapped;
    if ((old & bit(7)) == 0) return Status::RequiresSplit;
    MemoryAccess a = {};
    r = decode_leaf(s->backend, old, page_2m, &a);
    if (r != Status::Ok) return r;
    TablePage pt = {};
    r = allocate(s, &pt);
    if (r != Status::Ok) return r;
    const u64 base = old & phys_2m;
    for (u32 i = 0; i < 512; ++i) {
        u64 leaf = 0;
        r = encode_leaf(s->backend, base + static_cast<u64>(i) * page_4k,
                        page_4k, a, &leaf);
        if (r != Status::Ok) {
            s->provider.release(s->provider.user, pt);
            return r;
        }
        // Preserve supported EPT Ignore-PAT (bit 6) and A/D (8/9),
        // or NPT A/D (5/6) and Global (8) across large->small split.
        if (s->backend == Backend::IntelEpt) leaf |= old & (bit(6) | bit(8) | bit(9));
        else leaf |= old & (bit(5) | bit(6) | bit(8));
        pt.entries[i] = leaf;
    }
    r = modify_generation(s);
    if (r != Status::Ok) {
        s->provider.release(s->provider.user, pt);
        return r;
    }
    *parent = table_entry(s->backend, pt.physical_address);
    return Status::Ok;
}

Status protect_4k(AddressSpace* s, u64 gpa, MemoryAccess a) noexcept {
    if (!s || !s->initialized) return Status::BadArgument;
    u64* entry = nullptr;
    Status r = checked_leaf_slot(s, gpa, 4, &entry);
    if (r != Status::Ok) return r;
    if (!is_present(s->backend, *entry)) return Status::NotMapped;
    const u64 old = *entry;
    u64 encoded = 0;
    r = encode_leaf(s->backend, old & phys_4k, page_4k, a, &encoded);
    if (r != Status::Ok) return r;
    // Preserve A/D, and EPT leaf Ignore-PAT for permission-only change.
    if (s->backend == Backend::IntelEpt) encoded |= old & (bit(6) | bit(8) | bit(9));
    else encoded |= old & (bit(5) | bit(6) | bit(8));
    r = modify_generation(s);
    if (r == Status::Ok) *entry = encoded;
    return r;
}

Status unmap_4k(AddressSpace* s, u64 gpa) noexcept {
    if (!s || !s->initialized) return Status::BadArgument;
    u64* entry = nullptr;
    Status r = checked_leaf_slot(s, gpa, 4, &entry);
    if (r != Status::Ok) return r;
    if (!is_present(s->backend, *entry)) return Status::NotMapped;
    r = modify_generation(s);
    if (r == Status::Ok) *entry = 0;
    return r;
}

Status translate(const AddressSpace* s, u64 gpa, Translation* out) noexcept {
    if (!s || !s->initialized || !out) return Status::BadArgument;
    if ((gpa >> 48) != 0) return Status::BadAddressWidth;
    TablePage t = s->root;
    for (u32 level = 0; level < 4; ++level) {
        const u64 e = t.entries[index_for(gpa, level)];
        if (!is_present(s->backend, e)) return e == 0 ? Status::NotMapped : Status::CorruptTable;
        if (level >= 1 && (e & bit(7))) {
            if (level == 1) return Status::Unsupported; // 1 GiB leaves not supported
            const u64 mask = level == 2 ? page_2m - 1 : page_4k - 1;
            const u64 size = level == 2 ? page_2m : page_4k;
            MemoryAccess a = {};
            Status r = decode_leaf(s->backend, e, size, &a);
            if (r != Status::Ok) return r;
            *out = {(e & (size == page_2m ? phys_2m : phys_4k)) + (gpa & mask),
                    gpa & ~mask, size, a};
            return Status::Ok;
        }
        if (level == 3) {
            MemoryAccess a = {};
            Status r = decode_leaf(s->backend, e, page_4k, &a);
            if (r != Status::Ok) return r;
            *out = {(e & phys_4k) + (gpa & (page_4k - 1)),
                    gpa & ~(page_4k - 1), page_4k, a};
            return Status::Ok;
        }
        const u64 child = e & phys_4k;
        Status r = resolve(s, child, &t);
        if (r != Status::Ok) return r;
    }
    return Status::CorruptTable;
}
} // namespace hv
