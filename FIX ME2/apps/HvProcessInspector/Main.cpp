#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <stdlib.h>
#include "hv/process.h"

using hv::process::Code;
using hv::process::Result;
using hv::process::Target;

namespace {
void usage() noexcept {
    wprintf(L"HvProcessInspector - Windows x64 read-only memory inspection\n"
            L"  --list                            List running processes\n"
            L"  --pid 1234 [options]             Attach by PID\n"
            L"  --name TestApp.exe [options]    Attach by executable name\n"
            L"  --pid 1234 --module test.dll     Show module base and size\n"
            L"  --pid 1234 --regions 8          Show up to 8 readable regions\n"
            L"  --pid 1234 --read 0xADDR 64     Hex dump 64 bytes\n"
            L"  --pid 1234 --module test.dll --offset 0x100 --size 64\n"
            L"  --self-test                       Safe self-process read test\n"
            L"Note: only processes allowed by Windows access control. No privilege bypass.\n");
}

bool parse_number(const wchar_t* text, uint64_t* value) noexcept {
    if (!text || !*text || *text == L'-' || !value) return false;
    wchar_t* end = nullptr;
    const unsigned long long parsed = wcstoull(text, &end, 0);
    if (!end || *end != L'\0' || end == text) return false;
    *value = static_cast<uint64_t>(parsed);
    return true;
}

int report(Result result, const wchar_t* operation) noexcept {
    if (result.code == Code::Ok) return 0;
    fwprintf(stderr, L"%ls: %hs (Win32=%lu, transferred=%zu)\n",
             operation, hv::process::code_name(result.code),
             static_cast<unsigned long>(result.system_error), result.bytes_read);
    if (result.code == Code::AmbiguousName)
        fwprintf(stderr, L"Multiple matching processes. Use --list then --pid.\n");
    return 1;
}

void dump_bytes(uintptr_t address, const unsigned char* bytes, size_t count) noexcept {
    for (size_t start = 0; start < count; start += 16) {
        wprintf(L"%016llX  ", static_cast<unsigned long long>(address + start));
        for (size_t j = 0; j < 16; ++j) {
            if (start + j < count) wprintf(L"%02X ", bytes[start + j]);
            else wprintf(L"   ");
        }
        wprintf(L" | ");
        for (size_t j = 0; j < 16 && start + j < count; ++j) {
            const unsigned char v = bytes[start + j];
            wprintf(L"%lc", v >= 32 && v <= 126 ? static_cast<wchar_t>(v) : L'.');
        }
        wprintf(L"\n");
    }
}

int list_processes() noexcept {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"Cannot take process snapshot. Win32=%lu\n", GetLastError());
        return 1;
    }
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        const DWORD e = GetLastError();
        CloseHandle(snapshot);
        fwprintf(stderr, L"No processes. Win32=%lu\n", e);
        return 1;
    }
    wprintf(L"PID       Executable\n");
    do {
        wprintf(L"%-9lu %ls\n", entry.th32ProcessID, entry.szExeFile);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return 0;
}

int self_test() noexcept {
    alignas(16) volatile uint64_t marker = 0x1122334455667788ULL;
    Target target = {};
    Result result = hv::process::attach(&target, GetCurrentProcessId());
    if (report(result, L"Attach self")) return 1;
    uint64_t value = 0;
    result = hv::process::read(&target, reinterpret_cast<uintptr_t>(&marker), &value, sizeof(value));
    hv::process::detach(&target);
    if (report(result, L"Read self")) return 1;
    if (value != marker) { fwprintf(stderr, L"Self-test mismatch\n"); return 1; }
    wprintf(L"SELF-TEST PASS: read %016llX from PID %lu\n",
            static_cast<unsigned long long>(value), GetCurrentProcessId());
    return 0;
}

int show_regions(const Target* target, unsigned desired) noexcept {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    uintptr_t address = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t maximum = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    unsigned printed = 0;
    size_t scanned = 0;
    wprintf(L"Readable regions (first %u):\n", desired);
    while (address < maximum && printed < desired && scanned < 1000000) {
        ++scanned;
        hv::process::Region region = {};
        const Result result = hv::process::query_region(target, address, &region);
        if (result.code != Code::Ok) break;
        if (region.readable) {
            wprintf(L"  %016llX  %12zu  Protect=0x%lX\n",
                    static_cast<unsigned long long>(region.base), region.size,
                    static_cast<unsigned long>(region.protection));
            ++printed;
        }
        if (region.base > UINTPTR_MAX - region.size || region.size == 0) break;
        const uintptr_t next = region.base + region.size;
        if (next <= address) break;
        address = next;
    }
    wprintf(L"Listed %u regions\n", printed);
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc <= 1) { usage(); return 2; }
    if (argc == 2 && wcscmp(argv[1], L"--list") == 0) return list_processes();
    if (argc == 2 && wcscmp(argv[1], L"--self-test") == 0) return self_test();
    uint32_t pid = 0;
    const wchar_t* name = nullptr;
    const wchar_t* module_name = nullptr;
    bool do_read = false, read_by_offset = false;
    uint64_t address = 0, offset = 0, read_size = 64;
    unsigned region_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--pid") == 0 && i + 1 < argc) {
            uint64_t n = 0;
            if (!parse_number(argv[++i], &n) || n == 0 || n > UINT32_MAX) { usage(); return 2; }
            pid = static_cast<uint32_t>(n);
        } else if (wcscmp(argv[i], L"--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (wcscmp(argv[i], L"--module") == 0 && i + 1 < argc) {
            module_name = argv[++i];
        } else if (wcscmp(argv[i], L"--read") == 0 && i + 2 < argc) {
            do_read = parse_number(argv[++i], &address) && parse_number(argv[++i], &read_size);
            if (!do_read) { usage(); return 2; }
        } else if (wcscmp(argv[i], L"--offset") == 0 && i + 1 < argc) {
            if (!parse_number(argv[++i], &offset)) { usage(); return 2; }
            read_by_offset = true;
        } else if (wcscmp(argv[i], L"--size") == 0 && i + 1 < argc) {
            if (!parse_number(argv[++i], &read_size)) { usage(); return 2; }
        } else if (wcscmp(argv[i], L"--regions") == 0 && i + 1 < argc) {
            uint64_t n = 0;
            if (!parse_number(argv[++i], &n) || n > 32 || n == 0) { usage(); return 2; }
            region_count = static_cast<unsigned>(n);
        } else { usage(); return 2; }
    }
    if ((pid == 0 && !name) || (pid != 0 && name) || (read_by_offset && !module_name)) {
        usage(); return 2;
    }
    Target target = {};
    const Result attached = name ? hv::process::attach_by_name(&target, name)
                                 : hv::process::attach(&target, pid);
    if (report(attached, L"Attach process")) return 1;
    pid = target.pid;
    wchar_t real_name[260] = {};
    (void)hv::process::get_target_name(&target, real_name, 260);
    wprintf(L"Attached: %ls (PID %lu, read-only)\n", real_name, pid);
    hv::process::Module module = {};
    const Result module_result = module_name ? hv::process::find_module(&target, module_name, &module)
                                              : hv::process::main_module(&target, &module);
    const int module_error = report(module_result, L"Module lookup");
    if (!module_error) {
        wprintf(L"Module: %ls base=0x%016llX size=%zu\n", module.name,
                static_cast<unsigned long long>(module.base), module.size);
    }
    int result = 0;
    if (region_count) result = show_regions(&target, region_count);
    if (read_by_offset) {
        if (module_error || offset >= module.size || read_size > module.size - offset ||
            offset > UINTPTR_MAX - module.base) {
            fwprintf(stderr, L"Offset outside module or module unavailable\n");
            result = 1;
        } else { address = module.base + static_cast<uintptr_t>(offset); do_read = true; }
    }
    if (do_read) {
        if (read_size == 0 || read_size > 256 || address == 0 || address > UINTPTR_MAX) {
            fwprintf(stderr, L"Hex dump size must be between 1 and 256 bytes, and address valid.\n");
            result = 1;
        } else {
            unsigned char buffer[256] = {};
            Result read_result = hv::process::read(&target, static_cast<uintptr_t>(address),
                                                    buffer, static_cast<size_t>(read_size));
            if (report(read_result, L"Read memory")) result = 1;
            else dump_bytes(static_cast<uintptr_t>(address), buffer, read_result.bytes_read);
        }
    }
    hv::process::detach(&target);
    return result;
}
