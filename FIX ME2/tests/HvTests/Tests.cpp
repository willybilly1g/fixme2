#include <stdio.h>
#include "hv/paging.h"
#include "hv/sync.h"
#include "hv/exit.h"
#include "hv/cpu.h"

namespace {
constexpr hv::u32 slot_count = 512;
constexpr hv::u64 base_physical = 0x0000000100000000ULL;
struct Slot {
    alignas(4096) hv::u64 entries[512];
    bool used;
};
struct Pool {
    Slot slots[slot_count];
    hv::u32 in_use;
    hv::u32 calls;
    hv::u32 fail_on_call; // 1-based; 0 disabled
};
static Pool pool = {};
static hv::u32 failures = 0;
static hv::u32 checks = 0;

bool allocate(void* opaque, hv::TablePage* out) noexcept {
    Pool* p = static_cast<Pool*>(opaque);
    ++p->calls;
    if (p->fail_on_call && p->calls == p->fail_on_call) return false;
    for (hv::u32 i=0; i<slot_count; ++i) {
        if (!p->slots[i].used) {
            p->slots[i].used = true;
            ++p->in_use;
            *out = {base_physical + static_cast<hv::u64>(i)*hv::page_4k,
                    p->slots[i].entries};
            return true;
        }
    }
    return false;
}
bool resolve(void* opaque, hv::u64 pa, hv::TablePage* out) noexcept {
    Pool* p = static_cast<Pool*>(opaque);
    if (pa < base_physical || !hv::aligned(pa, hv::page_4k)) return false;
    const hv::u64 index = (pa - base_physical)/hv::page_4k;
    if (index >= slot_count || !p->slots[index].used) return false;
    *out = {pa, p->slots[index].entries};
    return true;
}
void release(void* opaque, hv::TablePage page) noexcept {
    Pool* p = static_cast<Pool*>(opaque);
    if (page.physical_address < base_physical) {++failures; return;}
    const hv::u64 index = (page.physical_address-base_physical)/hv::page_4k;
    if (index >= slot_count || !p->slots[index].used) {++failures; return;}
    p->slots[index].used = false;
    --p->in_use;
}
hv::PageProvider provider() {return {&pool, allocate, resolve, release};}
void expect(bool ok, const char* msg, int line) {
    ++checks;
    if (!ok) {++failures; printf("FAIL line %d: %s\n", line, msg);}
}
#define CHECK(cond) expect((cond), #cond, __LINE__)

void memory_case(hv::Backend backend) {
    hv::AddressSpace s = {};
    hv::MemoryAccess attr = hv::normal_ram();
    if (backend == hv::Backend::AmdNpt) {
        attr.npt_pat = true; attr.npt_write_through = true;
    }
    CHECK(hv::space_init(&s, backend, provider(), 48) == hv::Status::Ok);
    const hv::u64 gpa = 0x00400000ULL;
    const hv::u64 hpa = 0x00800000ULL;
    CHECK(hv::map_2m(&s, gpa, hpa, attr) == hv::Status::Ok);
    CHECK(hv::map_2m(&s, gpa, hpa, attr) == hv::Status::AlreadyMapped);
    hv::Translation tr = {};
    CHECK(hv::translate(&s, gpa + 0x1FFFFFULL, &tr) == hv::Status::Ok);
    CHECK(tr.page_size == hv::page_2m);
    CHECK(tr.host_physical_address == hpa + 0x1FFFFFULL);
    CHECK(hv::map_4k(&s, gpa+0x1000, 0xA00000, attr) == hv::Status::RequiresSplit);
    const hv::u64 old_gen = s.generation;
    CHECK(hv::split_2m(&s, gpa) == hv::Status::Ok);
    CHECK(s.generation == old_gen + 1);
    CHECK(hv::translate(&s, gpa + 0x1FFFFFULL, &tr) == hv::Status::Ok);
    CHECK(tr.page_size == hv::page_4k);
    CHECK(tr.host_physical_address == hpa + 0x1FFFFFULL);
    if (backend == hv::Backend::AmdNpt) {
        CHECK(tr.access.npt_pat);
        CHECK(tr.access.npt_write_through);
    }
    attr.write = false;
    attr.execute = false;
    CHECK(hv::protect_4k(&s, gpa+0x3000, attr) == hv::Status::Ok);
    CHECK(hv::translate(&s, gpa+0x3000, &tr) == hv::Status::Ok);
    CHECK(!tr.access.write && !tr.access.execute);
    CHECK(hv::translate(&s, gpa+0x4000, &tr) == hv::Status::Ok);
    CHECK(tr.access.write && tr.access.execute);
    CHECK(hv::unmap_4k(&s, gpa+0x3000) == hv::Status::Ok);
    CHECK(hv::translate(&s, gpa+0x3000, &tr) == hv::Status::NotMapped);
    CHECK(hv::translate(&s, gpa+0x4000, &tr) == hv::Status::Ok);
    CHECK(hv::space_destroy(&s) == hv::Status::Ok);
    CHECK(pool.in_use == 0);
}
void standalone_4k_case() {
    hv::AddressSpace s = {};
    hv::MemoryAccess a = hv::normal_ram();
    CHECK(hv::space_init(&s, hv::Backend::IntelEpt, provider(), 48) == hv::Status::Ok);
    for (hv::u32 i=0; i<200; ++i) {
        CHECK(hv::map_4k(&s, 0x100000000ULL+static_cast<hv::u64>(i)*hv::page_4k,
                             0x200000000ULL+static_cast<hv::u64>(i)*hv::page_4k, a) == hv::Status::Ok);
    }
    for (hv::u32 i=0; i<200; ++i) {
        hv::Translation t = {};
        CHECK(hv::translate(&s, 0x100000000ULL+static_cast<hv::u64>(i)*hv::page_4k+17, &t)
              == hv::Status::Ok);
        CHECK(t.host_physical_address == 0x200000000ULL+static_cast<hv::u64>(i)*hv::page_4k+17);
    }
    CHECK(hv::space_destroy(&s) == hv::Status::Ok);
    CHECK(pool.in_use == 0);
}
void rollback_case() {
    hv::AddressSpace s = {};
    CHECK(hv::space_init(&s, hv::Backend::AmdNpt, provider(), 48) == hv::Status::Ok);
    pool.fail_on_call = pool.calls + 2; // fail second private allocation
    CHECK(hv::map_2m(&s, 0x200000, 0x200000, hv::normal_ram()) == hv::Status::NoMemory);
    CHECK(pool.in_use == 1); // root only: no partial publish or leaked allocation
    hv::Translation t = {};
    CHECK(hv::translate(&s, 0x200000, &t) == hv::Status::NotMapped);
    pool.fail_on_call = 0;
    CHECK(hv::map_2m(&s, 0x200000, 0x200000, hv::normal_ram()) == hv::Status::Ok);
    pool.fail_on_call = pool.calls + 1;
    CHECK(hv::split_2m(&s, 0x200000) == hv::Status::NoMemory);
    CHECK(hv::translate(&s, 0x200000, &t) == hv::Status::Ok);
    CHECK(t.page_size == hv::page_2m);
    pool.fail_on_call = 0;
    CHECK(hv::space_destroy(&s) == hv::Status::Ok);
    CHECK(pool.in_use == 0);
}
void invalid_case() {
    hv::AddressSpace s = {};
    CHECK(hv::space_init(&s, hv::Backend::IntelEpt, provider(), 48) == hv::Status::Ok);
    CHECK(hv::map_2m(&s, 0x200001, 0x200000, hv::normal_ram()) == hv::Status::BadAlignment);
    CHECK(hv::map_4k(&s, 0x1000, 0x1000000000000ULL, hv::normal_ram()) == hv::Status::BadAddressWidth);
    hv::MemoryAccess invalid = hv::normal_ram();
    invalid.read = false;
    CHECK(hv::map_4k(&s, 0x1000, 0x2000, invalid) == hv::Status::BadArgument);
    invalid = hv::normal_ram();
    invalid.ept_memory_type = 5; // WC unsupported in this conservative backend
    CHECK(hv::map_4k(&s, 0x1000, 0x2000, invalid) == hv::Status::BadArgument);
    CHECK(hv::space_destroy(&s) == hv::Status::Ok);
    CHECK(pool.in_use == 0);
}
hv::ExitDecision valid_callback(void*, const hv::ExitEvent*) noexcept {
    return {hv::ExitAction::Resume, 2, 0};
}
hv::ExitDecision invalid_callback(void*, const hv::ExitEvent*) noexcept {
    return {hv::ExitAction::Resume, 0, 0};
}
void synchronization_and_exit_case() {
    hv::OfflineGenerationTracker t = {};
    CHECK(hv::tracker_init(&t, 32) == hv::Status::Ok);
    hv::u64 token = 0;
    CHECK(hv::tracker_publish(&t, &token) == hv::Status::Ok);
    CHECK(!hv::tracker_all_acknowledged(&t, token));
    for (hv::u32 cpu=0; cpu<32; ++cpu) {
        CHECK(hv::tracker_acknowledge(&t, cpu, token) == hv::Status::Ok);
    }
    CHECK(hv::tracker_all_acknowledged(&t, token));
    hv::ExitDispatcher dispatcher = {};
    hv::exit_dispatcher_init(&dispatcher);
    hv::ExitEvent e = {hv::ExitClass::Cpuid, 0, 0x1234, 0};
    CHECK(hv::exit_dispatch(&dispatcher, &e).action == hv::ExitAction::Stop);
    CHECK(hv::exit_dispatcher_register(&dispatcher, hv::ExitClass::Cpuid,
                                       valid_callback, nullptr) == hv::Status::Ok);
    hv::ExitDecision d = hv::exit_dispatch(&dispatcher, &e);
    CHECK(d.action == hv::ExitAction::Resume && d.instruction_length == 2);
    CHECK(hv::exit_dispatcher_register(&dispatcher, hv::ExitClass::Cpuid,
                                       invalid_callback, nullptr) == hv::Status::Ok);
    CHECK(hv::exit_dispatch(&dispatcher, &e).action == hv::ExitAction::Stop);
}
}
int main() {
    printf("HvTests - OFFLINE page-table model; not a hardware hypervisor test\n");
    memory_case(hv::Backend::IntelEpt);
    memory_case(hv::Backend::AmdNpt);
    standalone_4k_case();
    rollback_case();
    invalid_case();
    synchronization_and_exit_case();
    printf("Checks: %u | failures: %u\n", checks, failures);
    return failures ? 1 : 0;
}
