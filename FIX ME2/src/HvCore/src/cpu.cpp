#include "hv/cpu.h"
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

namespace hv {
namespace {
struct Regs { u32 eax, ebx, ecx, edx; };
Regs cpu_id(u32 leaf, u32 subleaf) noexcept {
    Regs r = {};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int data[4] = {};
    __cpuidex(data, static_cast<int>(leaf), static_cast<int>(subleaf));
    r = {static_cast<u32>(data[0]), static_cast<u32>(data[1]),
         static_cast<u32>(data[2]), static_cast<u32>(data[3])};
#elif defined(__i386__) || defined(__x86_64__)
    __cpuid_count(leaf, subleaf, r.eax, r.ebx, r.ecx, r.edx);
#else
    (void)leaf; (void)subleaf;
#endif
    return r;
}
void write_reg_bytes(char* dest, u32 reg) noexcept {
    for (u32 i = 0; i < 4; ++i) dest[i] = static_cast<char>((reg >> (i * 8)) & 255);
}
}

void detect_cpu(CpuCapabilities* out) noexcept {
    if (!out) return;
    *out = {};
    Regs r = cpu_id(0, 0);
    out->highest_basic_leaf = r.eax;
    write_reg_bytes(out->vendor_string, r.ebx);
    write_reg_bytes(out->vendor_string + 4, r.edx);
    write_reg_bytes(out->vendor_string + 8, r.ecx);
    out->vendor_string[12] = 0;
    const char* s = out->vendor_string;
    if (s[0]=='G' && s[1]=='e' && s[2]=='n' && s[3]=='u' && s[4]=='i' && s[5]=='n')
        out->vendor = CpuVendor::Intel;
    else if (s[0]=='A' && s[1]=='u' && s[2]=='t' && s[3]=='h' && s[4]=='e' && s[5]=='n')
        out->vendor = CpuVendor::Amd;
    if (out->highest_basic_leaf >= 1) {
        r = cpu_id(1, 0);
        out->has_vmx = (r.ecx & (1u << 5)) != 0;
        out->has_hypervisor_present = (r.ecx & (1u << 31)) != 0;
    }
    r = cpu_id(0x80000000u, 0);
    out->highest_extended_leaf = r.eax;
    if (out->highest_extended_leaf >= 0x80000001u) {
        r = cpu_id(0x80000001u, 0);
        out->has_svm = (r.ecx & (1u << 2)) != 0;
    }
    if (out->highest_extended_leaf >= 0x80000008u) {
        r = cpu_id(0x80000008u, 0);
        out->physical_address_bits = static_cast<u8>(r.eax & 255);
        out->linear_address_bits = static_cast<u8>((r.eax >> 8) & 255);
    }
    if (out->has_svm && out->highest_extended_leaf >= 0x8000000Au) {
        r = cpu_id(0x8000000Au, 0);
        out->max_asid = r.ebx;
        out->has_npt = (r.edx & 1u) != 0;
        out->has_flush_by_asid = (r.edx & (1u << 6)) != 0;
    }
}

const char* cpu_vendor_name(CpuVendor v) noexcept {
    switch (v) {
    case CpuVendor::Intel: return "Intel";
    case CpuVendor::Amd: return "AMD";
    default: return "Unknown";
    }
}
} // namespace hv
