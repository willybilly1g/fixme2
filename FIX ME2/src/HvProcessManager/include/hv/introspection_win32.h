#pragma once
#include "hv/introspection.h"
#include "hv/process.h"

// Bridges the already-authorized, read-only Windows process manager into the
// unified introspection API. This is NOT a VA->PA or hypervisor backend.
namespace hv::process {
hv::introspection::Status bind_introspection(const Target* target,
                                             hv::introspection::Context* channel) noexcept;
}
