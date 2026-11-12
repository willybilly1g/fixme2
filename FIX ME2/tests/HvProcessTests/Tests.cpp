#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "hv/process.h"

namespace {
unsigned checks = 0;
unsigned failures = 0;
void check(bool success, const char* name) noexcept {
    ++checks;
    if (!success) { ++failures; printf("FAIL: %s\n", name); }
}
}

int main() {
    using namespace hv::process;
    Target process = {};
    check(!is_attached(&process), "initial detached");
    check(attach(nullptr, 42).code == Code::InvalidArgument, "null target rejected");
    check(attach(&process, 0).code == Code::InvalidArgument, "PID zero rejected");
    Result result = attach(&process, GetCurrentProcessId());
    check(result.code == Code::Ok, "attach own process");
    if (result.code != Code::Ok) return 1;
    check(is_attached(&process), "attached");
    check(attach(&process, GetCurrentProcessId()).code == Code::InvalidArgument,
          "cannot overwrite live handle");
    bool alive = false;
    check(is_alive(&process, &alive).code == Code::Ok && alive, "process alive");
    wchar_t name[260] = {};
    check(get_process_name(GetCurrentProcessId(), name, 260).code == Code::Ok && name[0],
          "get own exe name");
    wchar_t owned_name[260] = {};
    check(get_target_name(&process, owned_name, 260).code == Code::Ok && owned_name[0],
          "read executable name from owned handle");
    Target process_by_name = {};
    const Result by_name = attach_by_name(&process_by_name, owned_name);
    check(by_name.code == Code::Ok || by_name.code == Code::AmbiguousName,
          "atomic attach by name or explicit ambiguity");
    if (by_name.code == Code::Ok) {
        check(process_by_name.pid == GetCurrentProcessId(), "name-attached correct PID");
        detach(&process_by_name);
    }
    uint32_t found_pid = 0;
    const Result search = find_pid(name, &found_pid);
    check((search.code == Code::Ok && found_pid == GetCurrentProcessId()) ||
          search.code == Code::AmbiguousName, "name search or explicit ambiguity");
    check(find_pid(L"____unlikely_missing_application_445566____.exe", &found_pid).code == Code::NotFound,
          "unknown executable not found");
    Module module = {};
    check(main_module(&process, &module).code == Code::Ok && module.base && module.size,
          "main module metadata");
    if (module.name[0]) {
        Module matching = {};
        check(find_module(&process, module.name, &matching).code == Code::Ok &&
                  matching.base == module.base, "module lookup by name");
    }
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);
    const size_t page = info.dwPageSize;
    unsigned char* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 3,
                                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(memory != nullptr, "allocate test memory");
    if (memory) {
        memory[0] = 0xA7;
        memory[1] = 0xB8;
        unsigned char buf[32] = {};
        result = read(&process, reinterpret_cast<uintptr_t>(memory), buf, 2);
        check(result.code == Code::Ok && result.bytes_read == 2 &&
              buf[0] == 0xA7 && buf[1] == 0xB8, "read own memory exact bytes");
        Region region = {};
        check(query_region(&process, reinterpret_cast<uintptr_t>(memory), &region).code == Code::Ok &&
              region.readable, "readable region query");
        DWORD previous = 0;
        check(VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous) != 0,
              "guard test page");
        check(query_region(&process, reinterpret_cast<uintptr_t>(memory + page), &region).code == Code::Ok &&
              !region.readable, "no-access region recognized");
        check(read(&process, reinterpret_cast<uintptr_t>(memory + page), buf, 8).code == Code::NotReadable,
              "no-access read rejected");
        check(read(&process, reinterpret_cast<uintptr_t>(memory + page - 8), buf, 16).code == Code::CrossesRegion,
              "cross region read rejected");
        check(read(&process, reinterpret_cast<uintptr_t>(memory), buf, max_read_bytes + 1).code == Code::ReadLimit,
              "maximum read limit");
        check(read(&process, UINTPTR_MAX - 3, buf, 8).code == Code::InvalidArgument,
              "address overflow rejected");
        check(read(&process, reinterpret_cast<uintptr_t>(memory), nullptr, 8).code == Code::InvalidArgument,
              "null read buffer rejected");
        VirtualFree(memory, 0, MEM_RELEASE);
    }
    detach(&process);
    check(!is_attached(&process), "detached");
    detach(&process);
    check(read(&process, 1, &found_pid, sizeof(found_pid)).code == Code::InvalidArgument,
          "detached read rejected");
    printf("HvProcessTests: checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
