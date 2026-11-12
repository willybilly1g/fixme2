#include "hv/introspection_hvcore_offline.h"

namespace hv::introspection {
namespace {
Status translate_offline(void* context, uint64_t guest_physical,
                         uint64_t* host_physical) noexcept {
    const auto* bridge = static_cast<const HvCoreOfflineBridge*>(context);
    if (!bridge || !bridge->second_level || !bridge->second_level->initialized ||
        !host_physical) return Status::InvalidArgument;
    hv::Translation translation = {};
    if (hv::translate(bridge->second_level, guest_physical, &translation) != hv::Status::Ok)
        return Status::TranslationUnavailable;
    if (!translation.access.read) return Status::PermissionDenied;
    *host_physical = translation.host_physical_address;
    return Status::Ok;
}
Status read_offline(void* context, uint64_t host_physical,
                    void* destination, size_t bytes) noexcept {
    const auto* bridge = static_cast<const HvCoreOfflineBridge*>(context);
    if (!bridge || !bridge->physical_read || !bridge->physical_context)
        return Status::InvalidArgument;
    return bridge->physical_read(bridge->physical_context, host_physical, destination, bytes);
}
uint64_t offline_generation(void* context) noexcept {
    const auto* bridge = static_cast<const HvCoreOfflineBridge*>(context);
    return bridge && bridge->second_level ? bridge->second_level->generation : 0;
}
}
Status bind_hvcore_offline(Context* context, HvCoreOfflineBridge* bridge,
                           uint64_t guest_cr3, uint8_t maximum_physical_bits) noexcept {
    if (!bridge || !bridge->second_level || !bridge->second_level->initialized ||
        !bridge->physical_context || !bridge->physical_read)
        return Status::InvalidArgument;
    return bind_offline_guest(context, bridge, translate_offline, read_offline,
                              guest_cr3, maximum_physical_bits,
                              bridge, offline_generation);
}
} // namespace hv::introspection
