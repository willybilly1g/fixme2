#pragma once
// Portable, no-STL orchestration model for an isolated 16-bit WHP guest.
// THIS DOES NOT CLAIM direct VMXON/VMRUN ownership or provide process introspection.
#include <stdint.h>
namespace hvwhp {
enum class Step : uint8_t { Empty, Partition, Setup, Memory, Vcpu, Executing, Halted, Failed, Closed };
enum class Event : uint8_t { Created, SetupDone, MemoryMapped, VcpuCreated, RunStarted, HltExit, Failure, Close };
constexpr Step transition(Step prior, Event e) noexcept {
    if (e == Event::Close) return Step::Closed;
    if (e == Event::Failure) return Step::Failed;
    if (prior == Step::Empty && e == Event::Created) return Step::Partition;
    if (prior == Step::Partition && e == Event::SetupDone) return Step::Setup;
    if (prior == Step::Setup && e == Event::MemoryMapped) return Step::Memory;
    if (prior == Step::Memory && e == Event::VcpuCreated) return Step::Vcpu;
    if (prior == Step::Vcpu && e == Event::RunStarted) return Step::Executing;
    if (prior == Step::Executing && e == Event::HltExit) return Step::Halted;
    return Step::Failed;
}
constexpr bool halted_successfully(Step s, uint64_t rax) noexcept {
    return s == Step::Halted && ((rax & 0xffffULL) == 0x1234ULL);
}
// A 16-bit x86 guest: mov ax, 0x1234 ; hlt
constexpr uint8_t guest_code[4] = {0xB8, 0x34, 0x12, 0xF4};
constexpr uint64_t guest_gpa = 0x1000;
constexpr uint64_t guest_memory_bytes = 4096;
}
