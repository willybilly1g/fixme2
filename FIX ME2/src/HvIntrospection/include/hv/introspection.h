#pragma once
#include <stddef.h>
#include <stdint.h>

// Explicit, allocation-free memory transport with two strictly separated backends.
// WindowsVirtual: documented read-only Win32 access, NO physical-translation claims.
// OfflineGuest: independently testable x64 guest page walk + second-level callback.
// The offline backend MUST NOT be used against running vCPUs without a verified
// TLB/IPI/safe-reclamation protocol and privileged, authorized physical IO.
namespace hv::introspection {

enum class Status : uint32_t {
    Ok = 0, InvalidArgument, Unsupported, InvalidCanonicalAddress,
    InvalidTable, NotPresent, PermissionDenied, TranslationUnavailable,
    ReadFailed, AddressOverflow, ExcessiveRequest, GenerationChanged
};

enum class Mode : uint8_t { WindowsVirtual, OfflineGuest };

using ReadVirtual = Status (*)(void* context, uint64_t virtual_address,
                               void* destination, size_t length) noexcept;
using TranslateSecondLevel = Status (*)(void* context, uint64_t guest_physical,
                                        uint64_t* host_physical) noexcept;
using ReadHostPhysical = Status (*)(void* context, uint64_t host_physical,
                                   void* destination, size_t length) noexcept;
using LoadGeneration = uint64_t (*)(void* context) noexcept;

struct Context {
    Mode mode;
    void* io_context;
    ReadVirtual read_virtual;
    TranslateSecondLevel translate_second_level;
    ReadHostPhysical read_host_physical;
    void* generation_context;
    LoadGeneration load_generation;
    uint64_t guest_cr3;
    uint8_t maximum_physical_address_bits;
};

struct Resolution {
    uint64_t guest_physical;
    uint64_t host_physical;
    uint64_t page_size;
    bool user_accessible;
    bool writable;
    bool executable;
};

struct Request {
    uint64_t virtual_address;
    void* destination;
    uint32_t bytes;
};

constexpr size_t maximum_batch_bytes = 64u * 1024u;
constexpr size_t maximum_batch_requests = 256u;

// Initializes a Windows-only virtual-memory transport (read callback may wrap
// OpenProcess/ReadProcessMemory). Never issues a privileged memory operation.
Status bind_windows(Context* out, void* context, ReadVirtual reader) noexcept;

// Initializes a page walker for testing against caller-controlled page-table
// images. Guest PTE reads AND requested guest memory use second-level translation.
// This is NOT a running VMX/SVM memory introspection implementation.
Status bind_offline_guest(Context* out, void* context,
                          TranslateSecondLevel translate,
                          ReadHostPhysical physical_read,
                          uint64_t cr3, uint8_t maximum_physical_bits,
                          void* generation_context,
                          LoadGeneration generation) noexcept;

// Translate a 48-bit canonical guest VA through four levels (1 GiB, 2 MiB and
// 4 KiB leaves). Returns an error for absent, privileged or malformed entries.
Status resolve_guest_address(const Context* context, uint64_t guest_va,
                             Resolution* resolved) noexcept;

// All-or-nothing destination commit. Scratch is a caller-owned staging buffer;
// destinations are untouched if ANY request fails or generation changes.
// Caller must provide a stable read-only snapshot of page mappings for the
// duration of the call; a generation check alone does not prevent races.
Status read_batch(const Context* context, const Request* requests,
                  size_t request_count, void* scratch, size_t scratch_size) noexcept;

const char* status_name(Status status) noexcept;
} // namespace hv::introspection
