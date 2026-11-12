#include <stdio.h>
#include "hv/cpu.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#endif
int main() {
    hv::CpuCapabilities c = {};
    hv::detect_cpu(&c);
    printf("HvProbe - Windows x64 / C++20 CPU capabilities (read-only)\n");
    printf("Vendor: %s (%s)\n", c.vendor_string, hv::cpu_vendor_name(c.vendor));
    printf("Physical/linear bits: %u / %u\n", c.physical_address_bits, c.linear_address_bits);
    printf("VMX: %s | SVM: %s | NPT: %s | ASIDs: %u | FlushByASID: %s\n",
           c.has_vmx ? "yes" : "no", c.has_svm ? "yes" : "no",
           c.has_npt ? "yes" : "no", c.max_asid,
           c.has_flush_by_asid ? "yes" : "no");
    printf("CPUID reports a hypervisor: %s\n", c.has_hypervisor_present ? "yes" : "no");
    printf("NOTE: CPUID is NOT proof that VMXON/VMRUN is available.\n");
#ifdef _WIN32
    HANDLE device = CreateFileW(L"\\\\.\\HvLabProbe", GENERIC_READ,
                                 FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        printf("Optional WDK probe driver: not installed (error %lu).\n", GetLastError());
    } else {
        constexpr DWORD ioctl = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA);
        struct Reply { unsigned int size, abi_version, feature_flags, reserved; } reply = {};
        DWORD bytes = 0;
        if (DeviceIoControl(device, ioctl, nullptr, 0, &reply, sizeof(reply), &bytes, nullptr)
            && bytes == sizeof(reply))
            printf("WDK probe driver ABI: %u, flags: 0x%08X\n", reply.abi_version, reply.feature_flags);
        else printf("WDK probe driver IOCTL failed (%lu).\n", GetLastError());
        CloseHandle(device);
    }
#endif
    return 0;
}
