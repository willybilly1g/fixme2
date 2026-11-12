#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <wchar.h>
#include "hv/process.h"

namespace hv::process {
namespace {
constexpr Result ok() noexcept { return {Code::Ok, 0, 0}; }
constexpr Result error(Code code, uint32_t win_error = 0) noexcept { return {code, win_error, 0}; }

Result win32_error() noexcept {
    const DWORD e = GetLastError();
    return error(e == ERROR_ACCESS_DENIED ? Code::AccessDenied : Code::Win32Error, e);
}

const wchar_t* file_name(const wchar_t* path) noexcept {
    if (!path) return nullptr;
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    return name;
}

bool matches(const wchar_t* a, const wchar_t* b) noexcept {
    return a && b && lstrcmpiW(a, b) == 0;
}

bool valid(const Target* target) noexcept {
    return target && target->handle && target->handle != INVALID_HANDLE_VALUE && target->pid != 0;
}

void copy_name(wchar_t* destination, size_t capacity, const wchar_t* source) noexcept {
    if (!destination || capacity == 0) return;
    size_t i = 0;
    while (i + 1 < capacity && source && source[i]) {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = L'\0';
}

bool readable_protection(DWORD protection) noexcept {
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (protection & 0xffu) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default: return false;
    }
}

HANDLE module_snapshot(uint32_t pid) noexcept {
    // Windows documents transient ERROR_BAD_LENGTH during loader changes.
    for (unsigned retry = 0; retry < 8; ++retry) {
        HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (handle != INVALID_HANDLE_VALUE) return handle;
        if (GetLastError() != ERROR_BAD_LENGTH) return INVALID_HANDLE_VALUE;
    }
    SetLastError(ERROR_BAD_LENGTH);
    return INVALID_HANDLE_VALUE;
}
}

Result find_pid(const wchar_t* executable_name, uint32_t* pid) noexcept {
    if (!executable_name || !*file_name(executable_name) || !pid) return error(Code::InvalidArgument);
    *pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return win32_error();
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        Result result = win32_error();
        CloseHandle(snapshot);
        return result;
    }
    uint32_t found = 0;
    unsigned count = 0;
    do {
        if (entry.th32ProcessID != 0 && matches(entry.szExeFile, file_name(executable_name))) {
            found = entry.th32ProcessID;
            if (++count > 1) break;
        }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (count == 0) return error(Code::NotFound);
    if (count > 1) return error(Code::AmbiguousName);
    *pid = found;
    return ok();
}

Result get_process_name(uint32_t pid, wchar_t* output, size_t capacity) noexcept {
    if (pid == 0 || !output || capacity == 0) return error(Code::InvalidArgument);
    output[0] = L'\0';
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return win32_error();
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        Result result = win32_error();
        CloseHandle(snapshot);
        return result;
    }
    bool found = false;
    do {
        if (entry.th32ProcessID == pid) {
            copy_name(output, capacity, entry.szExeFile);
            found = true;
            break;
        }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found ? ok() : error(Code::NotFound);
}

Result attach(Target* target, uint32_t pid) noexcept {
    if (!target || pid == 0 || target->handle != nullptr) return error(Code::InvalidArgument);
    // QUERY_INFORMATION for VirtualQueryEx; VM_READ for ReadProcessMemory;
    // SYNCHRONIZE for detecting target exit. Never ask for ALL_ACCESS.
    const DWORD permissions = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE;
    HANDLE handle = OpenProcess(permissions, FALSE, pid);
    if (!handle) return win32_error();
    target->handle = handle;
    target->pid = pid;
    return ok();
}

void detach(Target* target) noexcept {
    if (!target) return;
    if (target->handle && target->handle != INVALID_HANDLE_VALUE)
        CloseHandle(static_cast<HANDLE>(target->handle));
    target->handle = nullptr;
    target->pid = 0;
}

bool is_attached(const Target* target) noexcept { return valid(target); }

Result is_alive(const Target* target, bool* alive) noexcept {
    if (!valid(target) || !alive) return error(Code::InvalidArgument);
    *alive = false;
    DWORD result = WaitForSingleObject(static_cast<HANDLE>(target->handle), 0);
    if (result == WAIT_TIMEOUT) { *alive = true; return ok(); }
    if (result == WAIT_OBJECT_0) return ok();
    return win32_error();
}

Result get_target_name(const Target* target, wchar_t* output, size_t capacity) noexcept {
    if (!valid(target) || !output || capacity == 0 || capacity > 32767u)
        return error(Code::InvalidArgument);
    output[0] = L'\0';
    // Filename from owned process HANDLE (not a PID snapshot) avoids PID-reuse confusion.
    // QueryFullProcessImageNameW needs a full-path buffer; use a bounded stack buffer.
    wchar_t full_path[4096] = {};
    DWORD chars = static_cast<DWORD>(sizeof(full_path) / sizeof(full_path[0]));
    if (!QueryFullProcessImageNameW(static_cast<HANDLE>(target->handle), 0, full_path, &chars))
        return win32_error();
    copy_name(output, capacity, file_name(full_path));
    return ok();
}

Result attach_by_name(Target* target, const wchar_t* executable_name) noexcept {
    if (!target || target->handle || !executable_name || !*file_name(executable_name))
        return error(Code::InvalidArgument);
    uint32_t pid = 0;
    Result result = find_pid(executable_name, &pid);
    if (result.code != Code::Ok) return result;
    result = attach(target, pid);
    if (result.code != Code::Ok) return result;
    wchar_t actual_name[260] = {};
    result = get_target_name(target, actual_name, 260);
    if (result.code == Code::Ok && !matches(actual_name, file_name(executable_name)))
        result = error(Code::NotFound); // process exited/PID changed after snapshot
    if (result.code != Code::Ok) detach(target);
    return result;
}

Result find_module(const Target* target, const wchar_t* module_name, Module* module) noexcept {
    if (!valid(target) || !module_name || !*module_name || !module) return error(Code::InvalidArgument);
    *module = {};
    bool alive = false;
    Result live_result = is_alive(target, &alive);
    if (live_result.code != Code::Ok) return live_result;
    if (!alive) return error(Code::ProcessExited);
    HANDLE snapshot = module_snapshot(target->pid);
    if (snapshot == INVALID_HANDLE_VALUE) return win32_error();
    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot, &entry)) {
        Result result = win32_error();
        CloseHandle(snapshot);
        return result;
    }
    bool found = false;
    do {
        if (matches(entry.szModule, file_name(module_name))) {
            module->base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            module->size = static_cast<size_t>(entry.modBaseSize);
            copy_name(module->name, sizeof(module->name) / sizeof(module->name[0]), entry.szModule);
            found = true;
            break;
        }
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found ? ok() : error(Code::NotFound);
}

Result main_module(const Target* target, Module* module) noexcept {
    if (!valid(target) || !module) return error(Code::InvalidArgument);
    wchar_t name[MAX_PATH] = {};
    Result result = get_target_name(target, name, MAX_PATH);
    if (result.code != Code::Ok) return result;
    return find_module(target, name, module);
}

Result query_region(const Target* target, uintptr_t address, Region* region) noexcept {
    if (!valid(target) || !region || address == 0) return error(Code::InvalidArgument);
    *region = {};
    bool alive = false;
    Result live_result = is_alive(target, &alive);
    if (live_result.code != Code::Ok) return live_result;
    if (!alive) return error(Code::ProcessExited);
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQueryEx(static_cast<HANDLE>(target->handle),
                        reinterpret_cast<const void*>(address), &info, sizeof(info)))
        return win32_error();
    region->base = reinterpret_cast<uintptr_t>(info.BaseAddress);
    region->size = static_cast<size_t>(info.RegionSize);
    region->state = info.State;
    region->protection = info.Protect;
    region->type = info.Type;
    region->readable = info.State == MEM_COMMIT && readable_protection(info.Protect);
    return ok();
}

Result read(const Target* target, uintptr_t address, void* destination, size_t bytes) noexcept {
    if (!valid(target) || address == 0 || !destination || bytes == 0)
        return error(Code::InvalidArgument);
    if (bytes > max_read_bytes) return error(Code::ReadLimit);
    if (bytes - 1 > UINTPTR_MAX - address) return error(Code::InvalidArgument);
    Region region = {};
    Result status = query_region(target, address, &region);
    if (status.code != Code::Ok) return status;
    if (!region.readable) return error(Code::NotReadable);
    if (address < region.base || address - region.base >= region.size)
        return error(Code::NotReadable);
    if (bytes > region.size - (address - region.base))
        return error(Code::CrossesRegion);
    SIZE_T transferred = 0;
    BOOL done = ReadProcessMemory(static_cast<HANDLE>(target->handle),
                                  reinterpret_cast<const void*>(address),
                                  destination, bytes, &transferred);
    if (!done) {
        Result failure = win32_error();
        failure.bytes_read = transferred;
        return failure;
    }
    if (transferred != bytes) return {Code::PartialRead, 0, transferred};
    return {Code::Ok, 0, transferred};
}

const char* code_name(Code code) noexcept {
    switch (code) {
    case Code::Ok: return "Ok";
    case Code::InvalidArgument: return "InvalidArgument";
    case Code::NotFound: return "NotFound";
    case Code::AmbiguousName: return "AmbiguousName";
    case Code::AccessDenied: return "AccessDenied";
    case Code::ProcessExited: return "ProcessExited";
    case Code::NotReadable: return "NotReadable";
    case Code::CrossesRegion: return "CrossesRegion";
    case Code::ReadLimit: return "ReadLimit";
    case Code::PartialRead: return "PartialRead";
    case Code::Win32Error: return "Win32Error";
    }
    return "Unknown";
}
} // namespace hv::process
