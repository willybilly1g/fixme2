#pragma once
#include "hv/types.h"
namespace hv {
constexpr u32 max_test_vcpus = 256;
// Test-only protocol state: this does NOT emit IPI, INVEPT or NPT flushes.
// NEVER use the acknowledgment model as a hardware invalidation primitive.
struct OfflineGenerationTracker {
    u64 published_generation;
    u64 acknowledged_generation[max_test_vcpus];
    u32 cpu_count;
};
Status tracker_init(OfflineGenerationTracker* tracker, u32 cpu_count) noexcept;
Status tracker_publish(OfflineGenerationTracker* tracker, u64* token) noexcept;
Status tracker_acknowledge(OfflineGenerationTracker* tracker, u32 cpu, u64 token) noexcept;
bool tracker_all_acknowledged(const OfflineGenerationTracker* tracker, u64 token) noexcept;
} // namespace hv
