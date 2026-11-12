#include "hv/types.h"
namespace hv {
const char* status_name(Status status) noexcept {
    switch (status) {
    case Status::Ok: return "OK";
    case Status::BadArgument: return "BAD_ARGUMENT";
    case Status::BadAlignment: return "BAD_ALIGNMENT";
    case Status::BadAddressWidth: return "BAD_ADDRESS_WIDTH";
    case Status::Unsupported: return "UNSUPPORTED";
    case Status::NoMemory: return "NO_MEMORY";
    case Status::AlreadyMapped: return "ALREADY_MAPPED";
    case Status::NotMapped: return "NOT_MAPPED";
    case Status::RequiresSplit: return "REQUIRES_SPLIT";
    case Status::CorruptTable: return "CORRUPT_TABLE";
    case Status::Busy: return "BUSY";
    case Status::RollbackFailed: return "ROLLBACK_FAILED";
    case Status::SynchronizationRequired: return "SYNCHRONIZATION_REQUIRED";
    case Status::HandlerMissing: return "HANDLER_MISSING";
    }
    return "UNKNOWN";
}
}
