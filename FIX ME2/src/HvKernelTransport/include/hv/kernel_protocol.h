#pragma once
#include <stdint.h>

// Shared ABI: fixed-width, trivially copyable messages; no STL or pointers.
// These IOCTLs only return bounded diagnostics and a synthetic render sample.
// They do NOT read process memory or grant access to VMX/SVM/physical memory.
namespace hv::kernel_protocol {
constexpr uint32_t abi_version = 4;
constexpr uint32_t ioctl_status = (0x22u << 16) | (1u << 14) | (0x801u << 2);
constexpr uint32_t ioctl_process = (0x22u << 16) | (1u << 14) | (0x802u << 2);
constexpr uint32_t ioctl_rectangle = (0x22u << 16) | (1u << 14) | (0x803u << 2);
constexpr uint32_t ioctl_runtime = (0x22u << 16) | (1u << 14) | (0x804u << 2);
constexpr uint32_t ioctl_frame_publish = (0x22u << 16) | (2u << 14) | (0x805u << 2);
constexpr uint32_t ioctl_frame_read = (0x22u << 16) | (1u << 14) | (0x806u << 2);
constexpr uint32_t max_frame_boxes = 16;
constexpr uint32_t max_frame_dimension = 8192;

enum class Code : uint32_t { Ok=0, InvalidRequest=1, NotFound=2, Unsupported=3 };
enum Flags : uint32_t {
    driver_loaded = 1u,
    vmx_svm_active = 2u,
    physical_io_active = 4u,
    invalidation_active = 8u,
    transport_ready = 16u
};
struct Request { uint32_t size; uint32_t version; uint64_t request_id; };
struct StatusReply {
    uint32_t size; uint32_t version; uint64_t request_id;
    uint32_t code; uint32_t flags; uint32_t logical_processors; uint32_t reserved;
};
struct ProcessRequest { Request header; uint32_t pid; uint32_t reserved; };
struct ProcessReply {
    uint32_t size; uint32_t version; uint64_t request_id;
    uint32_t code; uint32_t pid; uint32_t found; uint32_t reserved;
};
struct RectangleReply {
    uint32_t size; uint32_t version; uint64_t request_id;
    uint32_t code; uint32_t health;
    int32_t x; int32_t y; int32_t width; int32_t height;
    uint32_t source; uint32_t reserved; // source=1 means synthetic kernel demo
};
// Explicit producer->kernel->renderer telemetry. Admin-only diagnostic device.
// The kernel NEVER reads memory of the game or an arbitrary process.
// Payload contains viewport-local, untrusted rectangles supplied by a producer.
struct FrameBox {
    int32_t x; int32_t y; int32_t width; int32_t height;
    uint32_t health; uint32_t object_id;
};
struct FramePayload {
    uint32_t count; uint32_t viewport_width; uint32_t viewport_height; uint32_t reserved;
    FrameBox boxes[max_frame_boxes];
};
struct FramePublish { Request header; uint64_t frame_sequence; FramePayload payload; };
struct FrameAck {
    uint32_t size; uint32_t version; uint64_t request_id;
    uint32_t code; uint32_t publisher_pid; uint64_t frame_sequence;
};
struct FrameReply {
    uint32_t size; uint32_t version; uint64_t request_id;
    uint32_t code; uint32_t publisher_pid; uint64_t frame_sequence;
    uint64_t timestamp_100ns;
    FramePayload payload;
};
bool validate_payload(const FramePayload* payload) noexcept;
bool validate_frame_ack(const FrameAck* response, uint64_t id, uint64_t sequence) noexcept;
bool validate_frame_reply(const FrameReply* response, uint64_t id) noexcept;
// Runtime diagnostics only; no physical addresses and no privileged VMX/SVM controls exposed.
enum RuntimeFeatures : uint32_t {
    cpu_vmx_capable = 1u << 0,
    cpu_svm_capable = 1u << 1,
    cpu_npt_reported = 1u << 2,
    host_hypervisor_present = 1u << 3,
    cpu_probe_complete = 1u << 4,
    own_page_allocated = 1u << 5,
    ipi_diagnostic_complete = 1u << 6
};
enum RuntimePhase : uint32_t { phase_uninitialized=0, phase_diagnostic_ready=1, phase_stopping=2 };
enum CpuVendor : uint32_t { vendor_unknown=0, vendor_intel=1, vendor_amd=2 };
struct RuntimeReply {
    uint32_t size; uint32_t version; uint64_t request_id;
    uint32_t code; uint32_t phase;
    uint32_t features; uint32_t vendor;
    uint32_t logical_processors; uint32_t ipi_acks;
    uint32_t owned_pages; uint32_t page_size;
    uint64_t generation; uint64_t reserved;
};
static_assert(sizeof(FrameBox) == 24);
static_assert(sizeof(FramePayload) == 400);
static_assert(sizeof(FramePublish) == 424);
static_assert(sizeof(FrameAck) == 32);
static_assert(sizeof(FrameReply) == 440);
static_assert(sizeof(RuntimeReply) == 64);
static_assert(sizeof(Request) == 16);
static_assert(sizeof(ProcessRequest) == 24);
static_assert(sizeof(StatusReply) == 32);
static_assert(sizeof(ProcessReply) == 32);
static_assert(sizeof(RectangleReply) == 48);

bool validate_status(const StatusReply* reply, uint64_t request_id) noexcept;
bool validate_process(const ProcessReply* reply, uint64_t request_id, uint32_t pid) noexcept;
bool validate_rectangle(const RectangleReply* reply, uint64_t request_id) noexcept;
bool validate_runtime(const RuntimeReply* reply, uint64_t request_id) noexcept;
} // namespace hv::kernel_protocol
