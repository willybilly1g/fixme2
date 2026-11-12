#pragma once
#include <stddef.h>
#include <stdint.h>

// Windows-only, read-only, user-mode process inspector.
// Explicit ownership, no STL / RTTI / exceptions / global process handles.
namespace hv::process {

enum class Code : uint32_t {
    Ok = 0,
    InvalidArgument,
    NotFound,
    AmbiguousName,
    AccessDenied,
    ProcessExited,
    NotReadable,
    CrossesRegion,
    ReadLimit,
    PartialRead,
    Win32Error
};

struct Result {
    Code code;
    uint32_t system_error; // GetLastError() on failed Win32 calls, otherwise 0
    size_t bytes_read;
};

struct Target {
    void* handle;          // owned HANDLE, never share outside of this API
    uint32_t pid;
};

struct Module {
    uintptr_t base;
    size_t size;
    wchar_t name[260];
};

struct Region {
    uintptr_t base;
    size_t size;
    uint32_t state;
    uint32_t protection;
    uint32_t type;
    bool readable;
};

// API constrains a single read to one committed readable region, max 64 KiB.
constexpr size_t max_read_bytes = 64u * 1024u;

Result find_pid(const wchar_t* executable_name, uint32_t* pid) noexcept;
Result get_process_name(uint32_t pid, wchar_t* output, size_t capacity) noexcept;
Result attach(Target* target, uint32_t pid) noexcept;
Result attach_by_name(Target* target, const wchar_t* executable_name) noexcept;
Result get_target_name(const Target* target, wchar_t* output, size_t capacity) noexcept;
void detach(Target* target) noexcept;
bool is_attached(const Target* target) noexcept;
Result is_alive(const Target* target, bool* alive) noexcept;
Result find_module(const Target* target, const wchar_t* module_name, Module* module) noexcept;
Result main_module(const Target* target, Module* module) noexcept;
Result query_region(const Target* target, uintptr_t address, Region* region) noexcept;
Result read(const Target* target, uintptr_t address, void* destination, size_t bytes) noexcept;
const char* code_name(Code code) noexcept;

} // namespace hv::process
