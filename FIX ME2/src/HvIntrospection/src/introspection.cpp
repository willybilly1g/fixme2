#include "hv/introspection.h"
#include <string.h>

namespace hv::introspection {
namespace {
constexpr uint64_t page_4k = 0x1000ull;
constexpr uint64_t page_2m = 0x200000ull;
constexpr uint64_t page_1g = 0x40000000ull;
constexpr uint64_t physical_52 = 0x000ffffffffff000ull;
constexpr uint64_t present_bit = 1ull;
constexpr uint64_t write_bit = 2ull;
constexpr uint64_t user_bit = 4ull;
constexpr uint64_t large_bit = 0x80ull;
constexpr uint64_t no_execute_bit = 1ull << 63;

bool canonical48(uint64_t address) noexcept {
    const uint64_t upper = address >> 48;
    return upper == (((address >> 47) & 1ull) != 0 ? 0xffffull : 0ull);
}

bool valid_physical(const Context* context, uint64_t page_entry) noexcept {
    const unsigned bits = context->maximum_physical_address_bits;
    const uint64_t allowed = (1ull << bits) - 1ull;
    return (page_entry & physical_52 & ~allowed) == 0;
}

bool add_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept {
    if (!out || b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
}

Status stage2(const Context* context, uint64_t gpa, uint64_t* hpa) noexcept {
    if (!context->translate_second_level) return Status::TranslationUnavailable;
    return context->translate_second_level(context->io_context, gpa, hpa);
}

Status guest_read(const Context* context, uint64_t guest_physical,
                  void* output, size_t count) noexcept {
    if (!context->read_host_physical || !output || count == 0)
        return Status::InvalidArgument;
    uint64_t host = 0;
    const Status translation = stage2(context, guest_physical, &host);
    if (translation != Status::Ok) return translation;
    return context->read_host_physical(context->io_context, host, output, count);
}

Status read_fragment(const Context* context, uint64_t address,
                     uint8_t* destination, size_t length) noexcept {
    if (context->mode == Mode::WindowsVirtual) {
        if (!context->read_virtual) return Status::Unsupported;
        return context->read_virtual(context->io_context, address, destination, length);
    }
    size_t copied = 0;
    while (copied < length) {
        uint64_t current = 0;
        if (!add_u64(address, static_cast<uint64_t>(copied), &current))
            return Status::AddressOverflow;
        Resolution mapped = {};
        const Status lookup = resolve_guest_address(context, current, &mapped);
        if (lookup != Status::Ok) return lookup;
        // Always split at 4-KiB boundaries to avoid a cross-page stage-2 read.
        const size_t available = static_cast<size_t>(page_4k - (current & (page_4k - 1)));
        const size_t amount = (length - copied < available) ? (length - copied) : available;
        if (!context->read_host_physical) return Status::Unsupported;
        const Status read = context->read_host_physical(context->io_context,
                                                        mapped.host_physical, destination + copied, amount);
        if (read != Status::Ok) return read;
        copied += amount;
    }
    return Status::Ok;
}
} // namespace

Status bind_windows(Context* out, void* context, ReadVirtual reader) noexcept {
    if (!out || !context || !reader) return Status::InvalidArgument;
    *out = {};
    out->mode = Mode::WindowsVirtual;
    out->io_context = context;
    out->read_virtual = reader;
    return Status::Ok;
}

Status bind_offline_guest(Context* out, void* context,
                          TranslateSecondLevel translate,
                          ReadHostPhysical physical_read,
                          uint64_t cr3, uint8_t maximum_physical_bits,
                          void* generation_context,
                          LoadGeneration generation) noexcept {
    if (!out || !context || !translate || !physical_read || !generation ||
        !generation_context || maximum_physical_bits < 36 ||
        maximum_physical_bits > 52 ||
        (cr3 & physical_52) == 0) return Status::InvalidArgument;
    *out = {};
    out->mode = Mode::OfflineGuest;
    out->io_context = context;
    out->translate_second_level = translate;
    out->read_host_physical = physical_read;
    out->guest_cr3 = cr3 & physical_52;
    out->maximum_physical_address_bits = maximum_physical_bits;
    out->generation_context = generation_context;
    out->load_generation = generation;
    return Status::Ok;
}

Status resolve_guest_address(const Context* context, uint64_t guest_va,
                             Resolution* resolved) noexcept {
    if (!context || !resolved) return Status::InvalidArgument;
    *resolved = {};
    if (context->mode != Mode::OfflineGuest || !context->read_host_physical ||
        !context->translate_second_level || context->maximum_physical_address_bits < 36 ||
        context->maximum_physical_address_bits > 52) return Status::Unsupported;
    if (!canonical48(guest_va)) return Status::InvalidCanonicalAddress;
    const unsigned shifts[4] = {39u, 30u, 21u, 12u};
    uint64_t table = context->guest_cr3 & physical_52;
    if (!table || !valid_physical(context, table)) return Status::InvalidTable;
    bool writable = true, user = true, executable = true;
    for (unsigned level = 0; level < 4; ++level) {
        const uint64_t slot = (guest_va >> shifts[level]) & 0x1ffull;
        uint64_t entry_pa = 0, entry = 0;
        if (!add_u64(table, slot * 8ull, &entry_pa)) return Status::AddressOverflow;
        const Status read = guest_read(context, entry_pa, &entry, sizeof(entry));
        if (read != Status::Ok) return read;
        if (!(entry & present_bit)) return Status::NotPresent;
        if (!valid_physical(context, entry)) return Status::InvalidTable;
        writable = writable && ((entry & write_bit) != 0);
        user = user && ((entry & user_bit) != 0);
        executable = executable && ((entry & no_execute_bit) == 0);
        // A user-mode process introspection request cannot access supervisor pages.
        if (!user) return Status::PermissionDenied;
        const bool large = (entry & large_bit) != 0;
        const bool last = level == 3;
        if (large && level == 0) return Status::InvalidTable;
        if (large && last) return Status::InvalidTable;
        if ((large && (level == 1 || level == 2)) || last) {
            const uint64_t size = last ? page_4k : (level == 1 ? page_1g : page_2m);
            const uint64_t base = (entry & physical_52) & ~(size - 1ull);
            // Reserved alignment bits of a huge leaf must be zero, except PAT (bit 12).
            if (!last && ((entry & physical_52 & (size - 1ull)) & ~0x1000ull) != 0)
                return Status::InvalidTable;
            uint64_t gpa = 0;
            if (!add_u64(base, guest_va & (size - 1ull), &gpa))
                return Status::AddressOverflow;
            uint64_t hpa = 0;
            const Status translation = stage2(context, gpa, &hpa);
            if (translation != Status::Ok) return translation;
            *resolved = {gpa, hpa, size, user, writable, executable};
            return Status::Ok;
        }
        table = entry & physical_52;
        if (!table) return Status::InvalidTable;
    }
    return Status::InvalidTable;
}

Status read_batch(const Context* context, const Request* requests,
                  size_t request_count, void* scratch, size_t scratch_size) noexcept {
    if (!context || !requests || !scratch || !request_count ||
        request_count > maximum_batch_requests) return Status::InvalidArgument;
    size_t total = 0;
    for (size_t i = 0; i < request_count; ++i) {
        if (!requests[i].destination || !requests[i].bytes ||
            requests[i].bytes > maximum_batch_bytes ||
            requests[i].bytes > maximum_batch_bytes - total ||
            !canonical48(requests[i].virtual_address) ||
            uint64_t(requests[i].bytes - 1) > UINT64_MAX - requests[i].virtual_address ||
            !canonical48(requests[i].virtual_address + requests[i].bytes - 1))
            return Status::InvalidArgument;
        total += requests[i].bytes;
    }
    if (total > scratch_size) return Status::ExcessiveRequest;
    if (context->mode == Mode::OfflineGuest &&
        (!context->load_generation || !context->generation_context))
        return Status::Unsupported;
    const uint64_t before = context->load_generation ?
        context->load_generation(context->generation_context) : 0;
    auto* staging = static_cast<uint8_t*>(scratch);
    size_t position = 0;
    for (size_t i = 0; i < request_count; ++i) {
        const Status result = read_fragment(context, requests[i].virtual_address,
                                            staging + position, requests[i].bytes);
        if (result != Status::Ok) return result;
        position += requests[i].bytes;
    }
    const uint64_t after = context->load_generation ?
        context->load_generation(context->generation_context) : 0;
    if (before != after) return Status::GenerationChanged;
    position = 0;
    for (size_t i = 0; i < request_count; ++i) {
        memcpy(requests[i].destination, staging + position, requests[i].bytes);
        position += requests[i].bytes;
    }
    return Status::Ok;
}

const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::Ok: return "OK";
    case Status::InvalidArgument: return "INVALID_ARGUMENT";
    case Status::Unsupported: return "UNSUPPORTED";
    case Status::InvalidCanonicalAddress: return "NON_CANONICAL_VA";
    case Status::InvalidTable: return "INVALID_PAGE_TABLE";
    case Status::NotPresent: return "NOT_PRESENT";
    case Status::PermissionDenied: return "PERMISSION_DENIED";
    case Status::TranslationUnavailable: return "TRANSLATION_UNAVAILABLE";
    case Status::ReadFailed: return "READ_FAILED";
    case Status::AddressOverflow: return "ADDRESS_OVERFLOW";
    case Status::ExcessiveRequest: return "EXCESSIVE_REQUEST";
    case Status::GenerationChanged: return "GENERATION_CHANGED";
    }
    return "UNKNOWN";
}
} // namespace hv::introspection
