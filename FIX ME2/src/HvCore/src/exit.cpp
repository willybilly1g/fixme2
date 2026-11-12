#include "hv/exit.h"
namespace hv {
void exit_dispatcher_init(ExitDispatcher* d) noexcept { if (d) *d = {}; }
Status exit_dispatcher_register(ExitDispatcher* d, ExitClass kind,
                                ExitHandler handler, void* user) noexcept {
    const u32 k = static_cast<u32>(kind);
    if (!d || !handler || k >= static_cast<u32>(ExitClass::Count))
        return Status::BadArgument;
    d->callbacks[k] = handler;
    d->callback_context[k] = user;
    return Status::Ok;
}
ExitDecision exit_dispatch(ExitDispatcher* d, const ExitEvent* e) noexcept {
    constexpr ExitDecision fail = {ExitAction::Stop, 0, 0};
    if (!d || !e) return fail;
    const u32 k = static_cast<u32>(e->kind);
    if (k >= static_cast<u32>(ExitClass::Count)) return fail;
    ++d->counts[k];
    ExitHandler fn = d->callbacks[k];
    if (!fn) return fail; // Fail-closed for unsupported hardware exits.
    ExitDecision decision = fn(d->callback_context[k], e);
    // This generic dispatcher never changes RIP or CPU state itself.
    if (decision.action != ExitAction::Resume && decision.instruction_length != 0)
        return fail;
    if (decision.action == ExitAction::Resume &&
        (decision.instruction_length == 0 || decision.instruction_length > 15))
        return fail;
    if (decision.action == ExitAction::Inject && decision.exception_vector > 31)
        return fail;
    return decision;
}
} // namespace hv
