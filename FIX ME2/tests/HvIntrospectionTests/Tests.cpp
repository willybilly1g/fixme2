#include <stdio.h>
#include <string.h>
#include "hv/introspection.h"
#include "hv/introspection_hvcore_offline.h"
#include "hv/paging.h"

namespace {
using hv::introspection::Status;
constexpr uint64_t gpa_base = 0x00400000ull;
constexpr uint64_t hpa_base = 0x00800000ull;
constexpr uint64_t table_root = 0x0000000200000000ull;
constexpr size_t sample_pages = 8;

struct alignas(4096) TableSlot {
    uint64_t entry[512];
    bool allocated;
};
struct Fixture {
    alignas(4096) unsigned char host[sample_pages][4096];
    TableSlot tables[32];
    hv::AddressSpace second;
    uint64_t epoch;
    unsigned physical_reads;
    bool bump_epoch_next;
};
static Fixture fixture = {};
static int errors = 0;
static int assertions = 0;
#define VERIFY(p) do {++assertions; if(!(p)){++errors; printf("FAIL line %d: %s\n",__LINE__,#p);}} while(0)

bool allocate(void* ctx, hv::TablePage* page) noexcept {
    auto* f = static_cast<Fixture*>(ctx);
    for (unsigned i = 0; i < 32; ++i) {
        if (!f->tables[i].allocated) {
            f->tables[i].allocated = true;
            memset(f->tables[i].entry, 0, sizeof(f->tables[i].entry));
            *page = {table_root + uint64_t(i) * 4096ull, f->tables[i].entry};
            return true;
        }
    }
    return false;
}
bool resolve_table(void* ctx, uint64_t physical, hv::TablePage* page) noexcept {
    auto* f = static_cast<Fixture*>(ctx);
    if (physical < table_root || (physical & 0xfffull)) return false;
    const uint64_t slot = (physical - table_root)/4096ull;
    if (slot >= 32 || !f->tables[slot].allocated) return false;
    *page = {physical, f->tables[slot].entry};
    return true;
}
void release(void* ctx, hv::TablePage table) noexcept {
    auto* f = static_cast<Fixture*>(ctx);
    if (table.physical_address < table_root) {++errors;return;}
    const uint64_t slot = (table.physical_address-table_root)/4096ull;
    if (slot>=32 || !f->tables[slot].allocated) {++errors;return;}
    f->tables[slot].allocated=false;
}
Status second_level(void* ctx, uint64_t gpa, uint64_t* hpa) noexcept {
    auto* f = static_cast<Fixture*>(ctx);
    hv::Translation translation = {};
    if (hv::translate(&f->second, gpa, &translation) != hv::Status::Ok)
        return Status::TranslationUnavailable;
    *hpa = translation.host_physical_address;
    return Status::Ok;
}
Status read_host(void* ctx, uint64_t hpa, void* dst, size_t count) noexcept {
    auto* f = static_cast<Fixture*>(ctx);
    if (hpa < hpa_base || count > 4096 || hpa - hpa_base >= sample_pages*4096 ||
        count > sample_pages*4096-(hpa-hpa_base)) return Status::ReadFailed;
    memcpy(dst, reinterpret_cast<unsigned char*>(f->host) + static_cast<size_t>(hpa-hpa_base), count);
    ++f->physical_reads;
    if (f->bump_epoch_next) { ++f->epoch; f->bump_epoch_next=false; }
    return Status::Ok;
}
uint64_t generation(void* ctx) noexcept {return static_cast<Fixture*>(ctx)->epoch;}
void write_entry(unsigned page, unsigned slot, uint64_t value) noexcept {
    memcpy(fixture.host[page] + slot*8u, &value, sizeof(value));
}

hv::introspection::Context setup(hv::Backend backend) noexcept {
    fixture = {};
    auto& f = fixture;
    VERIFY(hv::space_init(&f.second, backend, {&f, allocate, resolve_table, release}, 48) == hv::Status::Ok);
    for (unsigned i = 0; i < sample_pages; ++i) {
        VERIFY(hv::map_4k(&f.second, gpa_base + uint64_t(i)*4096ull,
                          hpa_base + uint64_t(i)*4096ull, hv::normal_ram()) == hv::Status::Ok);
    }
    // Guest page walk: PML4(0) -> PDPT(0) -> PD(0) -> PT(0/1) -> data pages(4/5).
    write_entry(0, 0, (gpa_base + 0x1000ull) | 7ull);
    write_entry(1, 0, (gpa_base + 0x2000ull) | 7ull);
    write_entry(2, 0, (gpa_base + 0x3000ull) | 7ull);
    write_entry(3, 0, (gpa_base + 0x4000ull) | 7ull);
    write_entry(3, 1, (gpa_base + 0x5000ull) | 7ull);
    for (unsigned i = 0; i < 16; ++i) {
        f.host[4][4096 - 16 + i] = static_cast<unsigned char>(0xa0 + i);
        f.host[5][i] = static_cast<unsigned char>(0xb0 + i);
    }
    hv::introspection::Context channel = {};
    VERIFY(hv::introspection::bind_offline_guest(&channel, &f, second_level,
        read_host, gpa_base, 48, &f, generation) == Status::Ok);
    return channel;
}

void test_small_pages(hv::Backend backend) noexcept {
    auto io = setup(backend);
    hv::introspection::Resolution resolved = {};
    VERIFY(hv::introspection::resolve_guest_address(&io, 0xff0, &resolved) == Status::Ok);
    VERIFY(resolved.guest_physical == gpa_base + 0x4ff0ull);
    VERIFY(resolved.host_physical == hpa_base + 0x4ff0ull);
    VERIFY(resolved.page_size == hv::page_4k && resolved.user_accessible && resolved.writable);
    unsigned char data[32] = {};
    unsigned char scratch[32] = {};
    const hv::introspection::Request request = {0xff0, data, sizeof(data)};
    VERIFY(hv::introspection::read_batch(&io, &request, 1, scratch, sizeof(scratch)) == Status::Ok);
    for (unsigned i = 0; i < 16; ++i) {
        VERIFY(data[i] == static_cast<unsigned char>(0xa0 + i));
        VERIFY(data[i+16] == static_cast<unsigned char>(0xb0 + i));
    }
    // Unmapped leaf causes no write to destination (all-or-nothing batch).
    write_entry(3, 1, 0);
    memset(data, 0xcc, sizeof(data));
    VERIFY(hv::introspection::read_batch(&io, &request, 1, scratch, sizeof(scratch)) == Status::NotPresent);
    VERIFY(data[0] == 0xcc && data[31] == 0xcc);
    write_entry(3, 1, (gpa_base + 0x5000ull) | 7ull);
    // Supervisor-only entry cannot be read as user process memory.
    write_entry(3, 0, (gpa_base + 0x4000ull) | 3ull);
    VERIFY(hv::introspection::resolve_guest_address(&io, 0xff0, &resolved) == Status::PermissionDenied);
    write_entry(3, 0, (gpa_base + 0x4000ull) | 7ull);
    // Explicit generation check blocks a potentially inconsistent read.
    fixture.bump_epoch_next = true;
    VERIFY(hv::introspection::read_batch(&io, &request, 1, scratch, sizeof(scratch)) == Status::GenerationChanged);
    VERIFY(data[0] == 0xcc && data[31] == 0xcc);
    VERIFY(hv::introspection::resolve_guest_address(&io, 0x0001000000000000ull, &resolved) ==
           Status::InvalidCanonicalAddress);
    VERIFY(hv::introspection::read_batch(&io, &request, 1, scratch, 4) == Status::ExcessiveRequest);
    VERIFY(hv::space_destroy(&fixture.second) == hv::Status::Ok);
}

void test_hvcore_bridge() noexcept {
    auto unused = setup(hv::Backend::AmdNpt);
    (void)unused;
    hv::introspection::HvCoreOfflineBridge bridge = {&fixture.second, &fixture, read_host};
    hv::introspection::Context channel = {};
    VERIFY(hv::introspection::bind_hvcore_offline(&channel, &bridge, gpa_base, 48) == Status::Ok);
    hv::introspection::Resolution result = {};
    VERIFY(hv::introspection::resolve_guest_address(&channel, 0xff0, &result) == Status::Ok);
    VERIFY(result.host_physical == hpa_base + 0x4ff0ull);
    unsigned char bytes[16] = {}, scratch[16] = {};
    hv::introspection::Request request = {0xff0ull, bytes, sizeof(bytes)};
    VERIFY(hv::introspection::read_batch(&channel, &request, 1, scratch, sizeof(scratch)) == Status::Ok);
    VERIFY(bytes[0] == 0xa0 && bytes[15] == 0xaf);
    VERIFY(hv::space_destroy(&fixture.second) == hv::Status::Ok);
}

void test_large_pages() noexcept {
    auto io = setup(hv::Backend::IntelEpt);
    // This entry uses the 2 MiB guest PDE leaf; EPT backing is independent.
    write_entry(2, 0, 0x00200000ull | 0x87ull);
    VERIFY(hv::map_2m(&fixture.second, 0x00200000ull, 0x00a00000ull,
                      hv::normal_ram()) == hv::Status::Ok);
    hv::introspection::Resolution t = {};
    VERIFY(hv::introspection::resolve_guest_address(&io, 0x1234ull, &t) == Status::Ok);
    VERIFY(t.page_size == hv::page_2m && t.guest_physical == 0x00201234ull &&
           t.host_physical == 0x00a01234ull);
    // Separate 1 GiB guest leaf, backed by a 2 MiB slice in second-level tables.
    write_entry(1, 0, 0x40000000ull | 0x87ull);
    VERIFY(hv::map_2m(&fixture.second, 0x40000000ull, 0x00c00000ull,
                      hv::normal_ram()) == hv::Status::Ok);
    VERIFY(hv::introspection::resolve_guest_address(&io, 0x1234ull, &t) == Status::Ok);
    VERIFY(t.page_size == hv::page_1g && t.guest_physical == 0x40001234ull &&
           t.host_physical == 0x00c01234ull);
    // Reserved bits in a huge entry fail closed.
    write_entry(1, 0, 0x40002000ull | 0x87ull);
    VERIFY(hv::introspection::resolve_guest_address(&io, 0x1234ull, &t) == Status::InvalidTable);
    VERIFY(hv::space_destroy(&fixture.second) == hv::Status::Ok);
}

struct VirtualFixture {
    unsigned char values[256];
    bool fail;
};
Status virtual_read(void* ctx, uint64_t va, void* dst, size_t bytes) noexcept {
    auto* f = static_cast<VirtualFixture*>(ctx);
    if (f->fail || va < 0x7000ull || va - 0x7000ull >= 256 ||
        bytes > 256 - (va-0x7000ull)) return Status::ReadFailed;
    memcpy(dst, f->values + (va-0x7000ull), bytes);
    return Status::Ok;
}
void test_windows_contract() noexcept {
    VirtualFixture v = {};
    for (unsigned i=0;i<256;++i) v.values[i] = static_cast<unsigned char>(i);
    hv::introspection::Context ctx = {};
    VERIFY(hv::introspection::bind_windows(&ctx, &v, virtual_read) == Status::Ok);
    unsigned char a[4]={8,8,8,8}, b[4]={9,9,9,9}, scratch[8]={};
    hv::introspection::Request two[2]={{0x7001ull,a,4},{0x7010ull,b,4}};
    VERIFY(hv::introspection::read_batch(&ctx, two, 2, scratch, sizeof(scratch)) == Status::Ok);
    VERIFY(a[0]==1 && a[3]==4 && b[0]==16 && b[3]==19);
    v.fail=true;
    VERIFY(hv::introspection::read_batch(&ctx, two, 2, scratch, sizeof(scratch)) == Status::ReadFailed);
    VERIFY(a[0]==1 && b[0]==16);
    v.fail=false;
    two[1].virtual_address=0x8000;
    VERIFY(hv::introspection::read_batch(&ctx, two, 2, scratch, sizeof(scratch)) == Status::ReadFailed);
    VERIFY(a[0]==1 && b[0]==16);
}
} // namespace
int main() {
    test_small_pages(hv::Backend::IntelEpt);
    test_small_pages(hv::Backend::AmdNpt);
    test_large_pages();
    test_hvcore_bridge();
    test_windows_contract();
    printf("HvIntrospectionTests: %d assertions; %d failures (offline model only)\n", assertions, errors);
    return errors ? 1 : 0;
}
