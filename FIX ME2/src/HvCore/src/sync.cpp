#include "hv/sync.h"
namespace hv {
Status tracker_init(OfflineGenerationTracker* t, u32 count) noexcept {
    if (!t || !count || count > max_test_vcpus) return Status::BadArgument;
    *t = {};
    t->cpu_count = count;
    return Status::Ok;
}
Status tracker_publish(OfflineGenerationTracker* t, u64* token) noexcept {
    if (!t || !token || !t->cpu_count || t->published_generation == ~0ULL)
        return Status::BadArgument;
    *token = ++t->published_generation;
    return Status::Ok;
}
Status tracker_acknowledge(OfflineGenerationTracker* t, u32 cpu, u64 token) noexcept {
    if (!t || cpu >= t->cpu_count || token == 0 || token > t->published_generation)
        return Status::BadArgument;
    if (token > t->acknowledged_generation[cpu]) t->acknowledged_generation[cpu] = token;
    return Status::Ok;
}
bool tracker_all_acknowledged(const OfflineGenerationTracker* t, u64 token) noexcept {
    if (!t || !t->cpu_count || token == 0 || token > t->published_generation) return false;
    for (u32 i = 0; i < t->cpu_count; ++i)
        if (t->acknowledged_generation[i] < token) return false;
    return true;
}
} // namespace hv
