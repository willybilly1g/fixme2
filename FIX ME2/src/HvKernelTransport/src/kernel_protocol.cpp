#include "hv/kernel_protocol.h"
namespace hv::kernel_protocol {
bool validate_status(const StatusReply* r, uint64_t id) noexcept {
    return r && r->size == sizeof(StatusReply) && r->version == abi_version &&
           r->request_id == id && r->code == static_cast<uint32_t>(Code::Ok) &&
           (r->flags & (driver_loaded | transport_ready)) == (driver_loaded | transport_ready) &&
           r->reserved == 0 && r->logical_processors > 0;
}
bool validate_process(const ProcessReply* r, uint64_t id, uint32_t pid) noexcept {
    return r && pid != 0 && r->size == sizeof(ProcessReply) && r->version == abi_version &&
           r->request_id == id && r->pid == pid && r->reserved == 0 && r->found <= 1 &&
           (r->code == static_cast<uint32_t>(Code::Ok) ||
            r->code == static_cast<uint32_t>(Code::NotFound));
}
bool validate_rectangle(const RectangleReply* r, uint64_t id) noexcept {
    return r && r->size == sizeof(RectangleReply) && r->version == abi_version &&
           r->request_id == id && r->code == static_cast<uint32_t>(Code::Ok) &&
           r->source == 1 && r->reserved == 0 &&
           r->width >= 8 && r->width <= 4096 && r->height >= 8 && r->height <= 4096 &&
           r->x >= 0 && r->x <= 8192 && r->y >= 0 && r->y <= 8192 && r->health <= 100;
}
bool validate_runtime(const RuntimeReply* r, uint64_t id) noexcept {
    if (!r || r->size != sizeof(RuntimeReply) || r->version != abi_version ||
        r->request_id != id || r->code != static_cast<uint32_t>(Code::Ok) ||
        r->reserved != 0 || r->page_size != 4096 || r->owned_pages != 1 ||
        r->phase != phase_diagnostic_ready || r->logical_processors == 0 ||
        r->ipi_acks > r->logical_processors || r->generation == 0) return false;
    if ((r->features & (cpu_probe_complete | own_page_allocated)) !=
        (cpu_probe_complete | own_page_allocated)) return false;
    if ((r->features & ipi_diagnostic_complete) && r->ipi_acks != r->logical_processors) return false;
    if (r->vendor != vendor_unknown && r->vendor != vendor_amd && r->vendor != vendor_intel) return false;
    if ((r->features & cpu_vmx_capable) && (r->features & cpu_svm_capable)) return false;
    return true;
}

}

namespace hv::kernel_protocol {
bool validate_payload(const FramePayload* p) noexcept {
    if (!p || p->reserved || p->count > max_frame_boxes ||
        !p->viewport_width || !p->viewport_height ||
        p->viewport_width > max_frame_dimension || p->viewport_height > max_frame_dimension) return false;
    for (uint32_t i=0; i<p->count; ++i) {
        const FrameBox& b=p->boxes[i];
        if (b.x < 0 || b.y < 0 || b.width < 8 || b.height < 8 || b.health > 100 || !b.object_id ||
            static_cast<uint32_t>(b.x) > p->viewport_width ||
            static_cast<uint32_t>(b.y) > p->viewport_height ||
            static_cast<uint32_t>(b.width) > p->viewport_width-static_cast<uint32_t>(b.x) ||
            static_cast<uint32_t>(b.height) > p->viewport_height-static_cast<uint32_t>(b.y)) return false;
    }
    return true;
}
bool validate_frame_ack(const FrameAck* r,uint64_t id,uint64_t seq) noexcept {
    return r && r->size==sizeof(FrameAck) && r->version==abi_version &&
           r->request_id==id && r->code==static_cast<uint32_t>(Code::Ok) &&
           r->publisher_pid && r->frame_sequence==seq;
}
bool validate_frame_reply(const FrameReply* r,uint64_t id) noexcept {
    if (!r || r->size!=sizeof(FrameReply) || r->version!=abi_version || r->request_id!=id) return false;
    if (r->code==static_cast<uint32_t>(Code::NotFound))
        return r->publisher_pid==0 && r->frame_sequence==0 && r->timestamp_100ns==0 &&
               r->payload.count==0;
    return r->code==static_cast<uint32_t>(Code::Ok) && r->publisher_pid &&
           r->frame_sequence && r->timestamp_100ns && validate_payload(&r->payload);
}
}
