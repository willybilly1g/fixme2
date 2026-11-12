#pragma once
#include "hv/kernel_protocol.h"
namespace hv::kernel_client {
enum class Code : uint32_t { Ok=0, InvalidArgument, NotConnected, WindowsError, BadReply };
struct Connection { void* handle; uint64_t sequence; };
struct Result { Code code; uint32_t win32_error; };
Result open(Connection* connection) noexcept;
// Open for a diagnostic telemetry producer, requiring administrative write access.
Result open_publisher(Connection* connection) noexcept;
void close(Connection* connection) noexcept;
Result status(Connection* connection, kernel_protocol::StatusReply* reply) noexcept;
Result runtime(Connection* connection, kernel_protocol::RuntimeReply* reply) noexcept;
Result process(Connection* connection, uint32_t pid, kernel_protocol::ProcessReply* reply) noexcept;
Result rectangle(Connection* connection, kernel_protocol::RectangleReply* reply) noexcept;
Result publish_frame(Connection* connection, uint64_t sequence,
                     const kernel_protocol::FramePayload* payload,
                     kernel_protocol::FrameAck* ack) noexcept;
Result read_frame(Connection* connection, kernel_protocol::FrameReply* reply) noexcept;
}
