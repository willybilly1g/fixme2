#pragma once
#include "hv/types.h"

namespace hv {
enum class CpuVendor : u8 { Unknown, Intel, Amd };
struct CpuCapabilities {
    char vendor_string[13];
    CpuVendor vendor;
    u32 highest_basic_leaf;
    u32 highest_extended_leaf;
    u32 max_asid;
    u8 physical_address_bits;
    u8 linear_address_bits;
    bool has_vmx;
    bool has_svm;
    bool has_npt;
    bool has_flush_by_asid;
    bool has_hypervisor_present;
    // CPUID alone does not confirm VMXON/SVM availability or Hyper-V ownership.
};
void detect_cpu(CpuCapabilities* out) noexcept;
const char* cpu_vendor_name(CpuVendor vendor) noexcept;
} // namespace hv
