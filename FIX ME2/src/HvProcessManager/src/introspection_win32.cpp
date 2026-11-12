#include "hv/introspection_win32.h"
#include <stdint.h>

namespace hv::process {
namespace {
hv::introspection::Status read_process(void* context, uint64_t address,
                                         void* destination, size_t count) noexcept {
    if (!context || !destination || address > UINTPTR_MAX)
        return hv::introspection::Status::InvalidArgument;
    const auto result = read(static_cast<const Target*>(context),
                             static_cast<uintptr_t>(address), destination, count);
    switch (result.code) {
    case Code::Ok: return hv::introspection::Status::Ok;
    case Code::AccessDenied: return hv::introspection::Status::PermissionDenied;
    case Code::InvalidArgument: return hv::introspection::Status::InvalidArgument;
    case Code::NotReadable: return hv::introspection::Status::PermissionDenied;
    default: return hv::introspection::Status::ReadFailed;
    }
}
}

hv::introspection::Status bind_introspection(const Target* target,
                                             hv::introspection::Context* channel) noexcept {
    if (!target || !is_attached(target) || !channel)
        return hv::introspection::Status::InvalidArgument;
    // A target handle belongs to the calling worker; do not detach or reuse
    // the handle while the channel is in use.
    return hv::introspection::bind_windows(channel,
                                          const_cast<Target*>(target), read_process);
}
}
