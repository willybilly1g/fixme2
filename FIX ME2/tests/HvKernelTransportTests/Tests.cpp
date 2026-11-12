#include "hv/kernel_protocol.h"
#include <stdio.h>
using namespace hv::kernel_protocol;
int main() {
  unsigned checked=0,failed=0;
  auto ck=[&](bool condition){++checked;if(!condition)++failed;};
  ck(sizeof(StatusReply)==32);ck(sizeof(ProcessReply)==32);ck(sizeof(RectangleReply)==48);
  StatusReply a={sizeof(a),abi_version,91,0,driver_loaded|transport_ready,8,0};
  ck(validate_status(&a,91));ck(!validate_status(&a,92));
  a.version++;ck(!validate_status(&a,91));a.version--;
  a.flags=driver_loaded;ck(!validate_status(&a,91));
  ProcessReply p={sizeof(p),abi_version,52,0,123,1,0};
  ck(validate_process(&p,52,123));ck(!validate_process(&p,52,124));
  p.code=static_cast<uint32_t>(Code::NotFound);p.found=0;ck(validate_process(&p,52,123));
  p.found=2;ck(!validate_process(&p,52,123));
  RectangleReply b={sizeof(b),abi_version,3,0,75,180,150,120,240,1,0};
  ck(validate_rectangle(&b,3));ck(!validate_rectangle(&b,4));
  b.health=101;ck(!validate_rectangle(&b,3));b.health=75;
  b.source=0;ck(!validate_rectangle(&b,3));
  RuntimeReply runtime = {sizeof(RuntimeReply),abi_version,501,0,phase_diagnostic_ready,
      cpu_probe_complete|own_page_allocated|ipi_diagnostic_complete,vendor_intel,8,8,1,4096,1,0};
  ck(validate_runtime(&runtime,501));
  ck(!validate_runtime(&runtime,502));
  runtime.ipi_acks=9;ck(!validate_runtime(&runtime,501));
  runtime.ipi_acks=7;ck(!validate_runtime(&runtime,501));
  runtime.features &= ~ipi_diagnostic_complete;ck(validate_runtime(&runtime,501));
  runtime.phase=phase_stopping;ck(!validate_runtime(&runtime,501));
  runtime.phase=phase_diagnostic_ready;runtime.features |= ipi_diagnostic_complete;
  runtime.ipi_acks=8;runtime.owned_pages=2;ck(!validate_runtime(&runtime,501));
  runtime.owned_pages=1;runtime.reserved=1;ck(!validate_runtime(&runtime,501));
  runtime.reserved=0;runtime.vendor=99;ck(!validate_runtime(&runtime,501));
  runtime.vendor=vendor_amd;runtime.features|=cpu_vmx_capable|cpu_svm_capable;ck(!validate_runtime(&runtime,501));
  // Fully specified, producer-owned live telemetry frames.
  ck(sizeof(FramePayload)==400);ck(sizeof(FramePublish)==424);
  ck(sizeof(FrameAck)==32);ck(sizeof(FrameReply)==440);
  FramePayload frame={};
  frame.viewport_width=800;frame.viewport_height=600;frame.count=2;
  frame.boxes[0]={100,120,70,180,80,1};
  frame.boxes[1]={350,140,60,160,55,2};
  ck(validate_payload(&frame));
  FrameReply reply={sizeof(FrameReply),abi_version,202,0,1012,10,500,frame};
  ck(validate_frame_reply(&reply,202));
  ck(!validate_frame_reply(&reply,203));
  reply.frame_sequence=0;ck(!validate_frame_reply(&reply,202));reply.frame_sequence=10;
  reply.publisher_pid=0;ck(!validate_frame_reply(&reply,202));reply.publisher_pid=1012;
  reply.timestamp_100ns=0;ck(!validate_frame_reply(&reply,202));reply.timestamp_100ns=500;
  frame.boxes[0].x=200; // moving box still passes validation.
  ck(validate_payload(&frame));
  reply.payload=frame;ck(validate_frame_reply(&reply,202));
  frame.boxes[0].x=-1;ck(!validate_payload(&frame));frame.boxes[0].x=200;
  frame.boxes[0].width=0;ck(!validate_payload(&frame));frame.boxes[0].width=70;
  frame.boxes[0].x=790;ck(!validate_payload(&frame));frame.boxes[0].x=200;
  frame.boxes[1].health=101;ck(!validate_payload(&frame));frame.boxes[1].health=55;
  frame.boxes[1].object_id=0;ck(!validate_payload(&frame));frame.boxes[1].object_id=2;
  frame.count=max_frame_boxes+1;ck(!validate_payload(&frame));frame.count=2;
  frame.viewport_width=0;ck(!validate_payload(&frame));frame.viewport_width=800;
  frame.viewport_height=max_frame_dimension+1;ck(!validate_payload(&frame));frame.viewport_height=600;
  frame.reserved=1;ck(!validate_payload(&frame));frame.reserved=0;
  frame.count=0;ck(validate_payload(&frame)); // explicit clear frame
  FrameAck ack={sizeof(FrameAck),abi_version,77,0,1012,999};
  ck(validate_frame_ack(&ack,77,999));
  ck(!validate_frame_ack(&ack,78,999));
  ck(!validate_frame_ack(&ack,77,1000));
  ack.publisher_pid=0;ck(!validate_frame_ack(&ack,77,999));
  reply.code=static_cast<uint32_t>(Code::NotFound);
  reply.frame_sequence=0;reply.timestamp_100ns=0;reply.publisher_pid=0;
  reply.payload={};
  ck(validate_frame_reply(&reply,202));
  reply.payload.count=1;ck(!validate_frame_reply(&reply,202));
  // Structural properties used by kernel access validation.
  ck((ioctl_frame_publish & 3u)==0u);ck((ioctl_frame_read & 3u)==0u);
  ck(((ioctl_frame_publish>>14)&3u)==2u);
  ck(((ioctl_frame_read>>14)&3u)==1u);
  printf("Kernel protocol tests: %u/%u PASS\n",checked-failed,checked);
  return failed?1:0;
}
