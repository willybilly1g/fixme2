// Actual Windows Hypervisor Platform guest execution on a supported Intel/AMD host.
// This is NOT direct per-CPU VMXON / AMD VMRUN; Hyper-V owns SLAT and hardware context.
// No host process scraping, kernel patching, or injection. C++20 / MSVC x64 / no STL.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <WinHvPlatform.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>
#include "GuestState.h"

#pragma comment(lib, "WinHvPlatform.lib")

namespace {
struct Runtime {
    WHV_PARTITION_HANDLE partition;
    void* guest_memory;
    bool vp_created;
    bool mapped;
    hvwhp::Step state;
};

static void print_error(const char* action, HRESULT result) {
    printf("[FAIL] %s: HRESULT=0x%08lX\n", action, (unsigned long)result);
}

static void cleanup(Runtime* r) {
    if (r->partition) {
        if (r->vp_created) {
            const HRESULT hr = WHvDeleteVirtualProcessor(r->partition, 0);
            if (FAILED(hr)) print_error("WHvDeleteVirtualProcessor", hr);
            r->vp_created = false;
        }
        if (r->mapped) {
            const HRESULT hr = WHvUnmapGpaRange(r->partition, hvwhp::guest_gpa, hvwhp::guest_memory_bytes);
            if (FAILED(hr)) print_error("WHvUnmapGpaRange", hr);
            r->mapped = false;
        }
        const HRESULT hr = WHvDeletePartition(r->partition);
        if (FAILED(hr)) print_error("WHvDeletePartition", hr);
        r->partition = nullptr;
    }
    if (r->guest_memory) {
        VirtualFree(r->guest_memory, 0, MEM_RELEASE);
        r->guest_memory = nullptr;
    }
    r->state = hvwhp::transition(r->state, hvwhp::Event::Close);
}

static void print_cpu_vendor() {
    int id[4] = {};
    __cpuid(id, 0);
    char vendor[13] = {};
    memcpy(vendor + 0, &id[1], 4);
    memcpy(vendor + 4, &id[3], 4);
    memcpy(vendor + 8, &id[2], 4);
    printf("Host CPU: %s; hardware virtualization controlled by Windows Hypervisor\n", vendor);
}

static void assign_segment(WHV_REGISTER_VALUE* reg, bool is_code) {
    memset(reg, 0, sizeof(*reg));
    reg->Segment.Base = 0;
    reg->Segment.Limit = 0xffff;
    reg->Segment.Selector = 0;
    // Real-mode-compatible segment attributes: present, user/system code/data.
    reg->Segment.Attributes = (USHORT)(is_code ? 0x9B : 0x93);
}

static HRESULT configure_real_mode(WHV_PARTITION_HANDLE partition) {
    const WHV_REGISTER_NAME names[] = {
        WHvX64RegisterRip, WHvX64RegisterRflags, WHvX64RegisterRax,
        WHvX64RegisterCr0, WHvX64RegisterCr3, WHvX64RegisterCr4,
        WHvX64RegisterEfer, WHvX64RegisterCs, WHvX64RegisterSs,
        WHvX64RegisterDs, WHvX64RegisterEs, WHvX64RegisterFs,
        WHvX64RegisterGs
    };
    WHV_REGISTER_VALUE values[sizeof(names)/sizeof(names[0])] = {};
    values[0].Reg64 = hvwhp::guest_gpa;
    values[1].Reg64 = 2;
    values[2].Reg64 = 0;
    values[3].Reg64 = 0x10;
    values[4].Reg64 = 0;
    values[5].Reg64 = 0;
    values[6].Reg64 = 0;
    for (unsigned i=7; i<sizeof(values)/sizeof(values[0]); ++i) assign_segment(&values[i], i==7);
    return WHvSetVirtualProcessorRegisters(partition, 0, names, (UINT32)(sizeof(names)/sizeof(names[0])), values);
}

static int run_guest() {
    Runtime r = {};
    r.state = hvwhp::Step::Empty;
    HRESULT hr = S_OK;
    int exit_code = 1;
    BOOL hypervisor_present = FALSE;
    UINT32 bytes_written = 0;
    print_cpu_vendor();
    hr = WHvGetCapability(WHvCapabilityCodeHypervisorPresent, &hypervisor_present,
                         sizeof(hypervisor_present), &bytes_written);
    if (FAILED(hr) || !hypervisor_present) {
        printf("WHP unavailable. Enable 'Windows Hypervisor Platform' in Windows Features,\n"
               "enable CPU virtualization in UEFI, reboot, and check Hyper-V/VBS policy.\n");
        if (FAILED(hr)) print_error("WHvGetCapability", hr);
        return 2;
    }
    do {
        hr = WHvCreatePartition(&r.partition);
        if (FAILED(hr)) { print_error("WHvCreatePartition",hr); break; }
        r.state = hvwhp::transition(r.state,hvwhp::Event::Created);
        const UINT32 count=1;
        hr = WHvSetPartitionProperty(r.partition, WHvPartitionPropertyCodeProcessorCount, &count, sizeof(count));
        if (FAILED(hr)) { print_error("WHvSetPartitionProperty",hr); break; }
        hr = WHvSetupPartition(r.partition);
        if (FAILED(hr)) { print_error("WHvSetupPartition",hr); break; }
        r.state = hvwhp::transition(r.state,hvwhp::Event::SetupDone);
        r.guest_memory=VirtualAlloc(nullptr, hvwhp::guest_memory_bytes, MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if (!r.guest_memory) { printf("[FAIL] VirtualAlloc: %lu\n",GetLastError()); break; }
        memcpy(r.guest_memory,hvwhp::guest_code,sizeof(hvwhp::guest_code));
        hr = WHvMapGpaRange(r.partition, r.guest_memory, hvwhp::guest_gpa, hvwhp::guest_memory_bytes,
                            (WHV_MAP_GPA_RANGE_FLAGS)(WHvMapGpaRangeFlagRead|WHvMapGpaRangeFlagWrite|WHvMapGpaRangeFlagExecute));
        if (FAILED(hr)) { print_error("WHvMapGpaRange",hr); break; }
        r.mapped=true;
        r.state=hvwhp::transition(r.state,hvwhp::Event::MemoryMapped);
        hr=WHvCreateVirtualProcessor(r.partition,0,0);
        if (FAILED(hr)) { print_error("WHvCreateVirtualProcessor",hr); break; }
        r.vp_created=true;
        r.state=hvwhp::transition(r.state,hvwhp::Event::VcpuCreated);
        hr=configure_real_mode(r.partition);
        if (FAILED(hr)) { print_error("WHvSetVirtualProcessorRegisters",hr); break; }
        r.state=hvwhp::transition(r.state,hvwhp::Event::RunStarted);
        printf("[RUN] Hardware-backed isolated guest, 4 KiB mapped at GPA 0x%llX\n",
               (unsigned long long)hvwhp::guest_gpa);
        WHV_RUN_VP_EXIT_CONTEXT exit_context = {};
        hr=WHvRunVirtualProcessor(r.partition,0,&exit_context,sizeof(exit_context));
        if (FAILED(hr)) { print_error("WHvRunVirtualProcessor",hr); break; }
        printf("[EXIT] VP exit reason: %u\n",(unsigned)exit_context.ExitReason);
        if (exit_context.ExitReason != WHvRunVpExitReasonX64Halt) {
            printf("[FAIL] Expected x86 HLT exit. Guest not reported as passed.\n"); break;
        }
        const WHV_REGISTER_NAME name=WHvX64RegisterRax;
        WHV_REGISTER_VALUE val={};
        hr=WHvGetVirtualProcessorRegisters(r.partition,0,&name,1,&val);
        if (FAILED(hr)) { print_error("WHvGetVirtualProcessorRegisters",hr); break; }
        r.state=hvwhp::transition(r.state,hvwhp::Event::HltExit);
        if (!hvwhp::halted_successfully(r.state,val.Reg64)) {
            printf("[FAIL] Wrong guest AX=0x%04llX (expected 0x1234)\n",(unsigned long long)(val.Reg64&0xffff)); break;
        }
        printf("[PASS] Guest executed MOV AX,0x1234 and HLT (AX=%04llX).\n",(unsigned long long)(val.Reg64&0xffff));
        exit_code=0;
    } while (false);
    if (exit_code) r.state=hvwhp::transition(r.state,hvwhp::Event::Failure);
    cleanup(&r);
    return exit_code;
}
}

int main(int argc, char** argv) {
    if(argc>1 && (!strcmp(argv[1],"--help")||!strcmp(argv[1],"/?"))) {
        puts("HvWHPGuest: isolated hardware-backed Windows Hypervisor Platform smoke test.\n"
             "No WDK driver and no process/game hook. Must enable Windows Hypervisor Platform feature.");
        return 0;
    }
    return run_guest();
}
