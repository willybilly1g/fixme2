#pragma once
#include "hv/introspection.h"
#include "hv/paging.h"

namespace hv::introspection {
// Explicit compatibility bridge to the existing HvCore offline EPT/NPT manager.
// Caller owns the AddressSpace, the guest CR3 fixture and host physical reader.
// This bridge is for test/quiesced mappings only; it does NOT invalidate TLBs.
struct HvCoreOfflineBridge {
    const hv::AddressSpace* second_level;
    void* physical_context;
    ReadHostPhysical physical_read;
};

Status bind_hvcore_offline(Context* context, HvCoreOfflineBridge* bridge,
                           uint64_t guest_cr3, uint8_t maximum_physical_bits) noexcept;
}
