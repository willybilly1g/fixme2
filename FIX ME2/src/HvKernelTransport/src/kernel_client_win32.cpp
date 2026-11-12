#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "hv/kernel_client.h"
namespace hv::kernel_client {
namespace {
Result fail(Code code) noexcept { return { code, static_cast<uint32_t>(GetLastError()) }; }
Result call(Connection* c, uint32_t ioctl, void* request, uint32_t in_size,
            void* reply, uint32_t out_size) noexcept {
    if (!c || !c->handle || !request || !reply) return {Code::NotConnected, 0};
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(static_cast<HANDLE>(c->handle), ioctl, request,
                              in_size, reply, out_size, &returned, nullptr);
    if (!ok) return fail(Code::WindowsError);
    return returned == out_size ? Result{Code::Ok, 0} : Result{Code::BadReply, 0};
}
}
Result open(Connection* c) noexcept {
    if (!c || c->handle) return {Code::InvalidArgument, 0};
    HANDLE h = CreateFileW(L"\\\\.\\HvLabProbe", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail(Code::WindowsError);
    c->handle = h;
    c->sequence = 0;
    return {Code::Ok,0};
}
void close(Connection* c) noexcept {
    if (c && c->handle) { CloseHandle(static_cast<HANDLE>(c->handle)); c->handle=nullptr; c->sequence=0; }
}
Result status(Connection* c, kernel_protocol::StatusReply* out) noexcept {
    if (!c || !out || !c->handle) return {Code::InvalidArgument,0};
    kernel_protocol::Request req = {sizeof(req), kernel_protocol::abi_version, ++c->sequence};
    kernel_protocol::StatusReply rep = {};
    auto r = call(c,kernel_protocol::ioctl_status,&req,sizeof(req),&rep,sizeof(rep));
    if (r.code != Code::Ok) return r;
    if (!kernel_protocol::validate_status(&rep,req.request_id)) return {Code::BadReply,0};
    *out = rep; return r;
}
Result process(Connection* c, uint32_t pid, kernel_protocol::ProcessReply* out) noexcept {
    if (!c || !out || !c->handle || !pid) return {Code::InvalidArgument,0};
    kernel_protocol::ProcessRequest req = {{sizeof(req),kernel_protocol::abi_version,++c->sequence},pid,0};
    kernel_protocol::ProcessReply rep = {};
    auto r = call(c,kernel_protocol::ioctl_process,&req,sizeof(req),&rep,sizeof(rep));
    if (r.code != Code::Ok) return r;
    if (!kernel_protocol::validate_process(&rep,req.header.request_id,pid)) return {Code::BadReply,0};
    *out=rep; return r;
}
Result rectangle(Connection* c, kernel_protocol::RectangleReply* out) noexcept {
    if (!c || !out || !c->handle) return {Code::InvalidArgument,0};
    kernel_protocol::Request req = {sizeof(req),kernel_protocol::abi_version,++c->sequence};
    kernel_protocol::RectangleReply rep = {};
    auto r = call(c,kernel_protocol::ioctl_rectangle,&req,sizeof(req),&rep,sizeof(rep));
    if (r.code != Code::Ok) return r;
    if (!kernel_protocol::validate_rectangle(&rep,req.request_id)) return {Code::BadReply,0};
    *out=rep; return r;
}
}

namespace hv::kernel_client {
Result runtime(Connection* c, kernel_protocol::RuntimeReply* out) noexcept {
    if (!c || !out || !c->handle) return {Code::InvalidArgument, 0};
    kernel_protocol::Request req = {sizeof(req), kernel_protocol::abi_version, ++c->sequence};
    kernel_protocol::RuntimeReply rep = {};
    const auto result = call(c, kernel_protocol::ioctl_runtime, &req, sizeof(req), &rep, sizeof(rep));
    if (result.code != Code::Ok) return result;
    if (!kernel_protocol::validate_runtime(&rep, req.request_id)) return {Code::BadReply, 0};
    *out = rep;
    return result;
}
}

namespace hv::kernel_client {
Result open_publisher(Connection* c) noexcept {
    if (!c || c->handle) return {Code::InvalidArgument,0};
    HANDLE h=CreateFileW(L"\\\\.\\HvLabProbe",GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (h==INVALID_HANDLE_VALUE) return {Code::WindowsError,static_cast<uint32_t>(GetLastError())};
    c->handle=h;c->sequence=0;
    return {Code::Ok,0};
}
Result publish_frame(Connection* c,uint64_t sequence,const kernel_protocol::FramePayload* payload,
                     kernel_protocol::FrameAck* output) noexcept {
    if (!c || !c->handle || !payload || !output || !sequence ||
        !kernel_protocol::validate_payload(payload)) return {Code::InvalidArgument,0};
    kernel_protocol::FramePublish input={};
    input.header={sizeof(input),kernel_protocol::abi_version,++c->sequence};
    input.frame_sequence=sequence;
    input.payload=*payload;
    kernel_protocol::FrameAck result={};
    auto status=call(c,kernel_protocol::ioctl_frame_publish,&input,sizeof(input),&result,sizeof(result));
    if(status.code!=Code::Ok) return status;
    if(!kernel_protocol::validate_frame_ack(&result,input.header.request_id,sequence))return {Code::BadReply,0};
    *output=result;
    return status;
}
Result read_frame(Connection* c,kernel_protocol::FrameReply* output) noexcept {
    if (!c || !c->handle || !output) return {Code::InvalidArgument,0};
    kernel_protocol::Request input={sizeof(input),kernel_protocol::abi_version,++c->sequence};
    kernel_protocol::FrameReply result={};
    auto status=call(c,kernel_protocol::ioctl_frame_read,&input,sizeof(input),&result,sizeof(result));
    if(status.code!=Code::Ok)return status;
    if(!kernel_protocol::validate_frame_reply(&result,input.request_id))return {Code::BadReply,0};
    *output=result;
    return status;
}
}
