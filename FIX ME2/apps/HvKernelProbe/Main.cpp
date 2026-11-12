#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "hv/kernel_client.h"
int wmain(int argc,wchar_t** argv){
  hv::kernel_client::Connection c={};
  auto r=hv::kernel_client::open(&c);
  if(r.code!=hv::kernel_client::Code::Ok){wprintf(L"Driver unavailable (Win32=%u). Install signed WDK test driver first.\n",r.win32_error);return 2;}
  hv::kernel_protocol::StatusReply s={};r=hv::kernel_client::status(&c,&s);
  if(r.code!=hv::kernel_client::Code::Ok){printf("status IOCTL failed: %u\n",r.win32_error);hv::kernel_client::close(&c);return 3;}
  printf("Driver: ready, processors=%u, flags=0x%X; active VMX/SVM=%s; active physical IO=%s\n",s.logical_processors,s.flags,(s.flags&hv::kernel_protocol::vmx_svm_active)?"YES":"NO",(s.flags&hv::kernel_protocol::physical_io_active)?"YES":"NO");
  hv::kernel_protocol::RuntimeReply diag = {};
  r = hv::kernel_client::runtime(&c,&diag);
  if(r.code==hv::kernel_client::Code::Ok){
    const char* vendor = diag.vendor==hv::kernel_protocol::vendor_intel?"Intel":
                         diag.vendor==hv::kernel_protocol::vendor_amd?"AMD":"unknown";
    printf("Kernel diagnostics: vendor=%s features=0x%X phase=%u owned-pages=%u generation=%llu\n",
           vendor, diag.features, diag.phase, diag.owned_pages,
           static_cast<unsigned long long>(diag.generation));
    printf("IPI diagnostic: acknowledgements=%u expected=%u (NOT a TLB flush)\n",
           diag.ipi_acks,diag.logical_processors);
    printf("Hardware virtualization runtime: NOT IMPLEMENTED; guest CR3/memory reading: NOT IMPLEMENTED.\n");
  } else {
    printf("Runtime diagnostics IOCTL failed: %u (Win32=%u)\n",static_cast<unsigned>(r.code),r.win32_error);
    hv::kernel_client::close(&c);
    return 6;
  }
  hv::kernel_protocol::FrameReply telemetry={};
  if(hv::kernel_client::read_frame(&c,&telemetry).code==hv::kernel_client::Code::Ok){
    if(telemetry.code==static_cast<uint32_t>(hv::kernel_protocol::Code::Ok)){
      printf("Bounded telemetry: producer PID=%u frame=%llu boxes=%u viewport=%ux%u\n",
             telemetry.publisher_pid, static_cast<unsigned long long>(telemetry.frame_sequence),
             telemetry.payload.count,telemetry.payload.viewport_width,telemetry.payload.viewport_height);
    } else printf("Bounded telemetry: no fresh frame (expires after 500ms)\n");
  }
  if(argc>1){ wchar_t* end=nullptr;unsigned long pid=wcstoul(argv[1],&end,10);
    if(!pid || (end && *end)){printf("PID must be a positive decimal number\n");hv::kernel_client::close(&c);return 4;}
    hv::kernel_protocol::ProcessReply p={};r=hv::kernel_client::process(&c,static_cast<uint32_t>(pid),&p);
    if(r.code!=hv::kernel_client::Code::Ok){printf("process query failed: %u\n",r.win32_error);hv::kernel_client::close(&c);return 5;}
    printf("Kernel PID lookup: %u => %s (does not acquire CR3 or read process memory)\n",p.pid,p.found?"found":"not found");
  }
  hv::kernel_client::close(&c);
  return 0;
}
