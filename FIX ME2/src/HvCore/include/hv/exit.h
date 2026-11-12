#pragma once
#include "hv/types.h"
namespace hv {
enum class ExitClass : u8 {
    Cpuid, MsrRead, MsrWrite, EptViolation, NptFault, Exception, Other, Count
};
enum class ExitAction : u8 { Stop, Retry, Inject, Resume };
struct ExitEvent {
    ExitClass kind;
    u64 qualification;
    u64 guest_rip;
    u64 guest_physical_address;
};
struct ExitDecision {
    ExitAction action;
    u32 instruction_length;  // 0 for Retry/Inject/Stop; explicit for Resume.
    u32 exception_vector;    // meaningful only for Inject.
};
using ExitHandler = ExitDecision (*)(void* user, const ExitEvent* event) noexcept;
struct ExitDispatcher {
    ExitHandler callbacks[static_cast<u32>(ExitClass::Count)];
    void* callback_context[static_cast<u32>(ExitClass::Count)];
    u64 counts[static_cast<u32>(ExitClass::Count)];
};
void exit_dispatcher_init(ExitDispatcher* dispatcher) noexcept;
Status exit_dispatcher_register(ExitDispatcher* dispatcher, ExitClass kind,
                                ExitHandler handler, void* context) noexcept;
ExitDecision exit_dispatch(ExitDispatcher* dispatcher, const ExitEvent* event) noexcept;
} // namespace hv
