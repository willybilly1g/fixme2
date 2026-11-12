#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "hv/kernel_client.h"

// A cooperative test producer owns both objects and their screen coordinates.
// It publishes ONLY documented telemetry; no game memory access or DLL hooking.
int wmain(int argc,wchar_t** argv) {
    unsigned seconds=30;
    if(argc==2) {
        wchar_t* end=nullptr;
        const unsigned long number=wcstoul(argv[1],&end,10);
        if(!number || number>3600 || !end || *end) {
            wprintf(L"Usage: HvTelemetryPublisher.exe [duration_seconds 1..3600]\n");return 1;
        }
        seconds=static_cast<unsigned>(number);
    }
    hv::kernel_client::Connection client={};
    auto result=hv::kernel_client::open_publisher(&client);
    if(result.code!=hv::kernel_client::Code::Ok) {
        printf("Cannot open WDK diagnostic device for publishing (run elevated). Win32=%u\n",result.win32_error);
        return 2;
    }
    const ULONGLONG begin=GetTickCount64();
    uint64_t sequence=0;
    while(GetTickCount64()-begin<static_cast<ULONGLONG>(seconds)*1000ull) {
        const ULONGLONG elapsed=GetTickCount64()-begin;
        const uint32_t phase=static_cast<uint32_t>((elapsed/10ull)%1100ull);
        const uint32_t step=phase<550u?phase:(1100u-phase);
        hv::kernel_protocol::FramePayload frame={};
        frame.count=2;frame.viewport_width=800;frame.viewport_height=600;
        frame.boxes[0]={60+static_cast<int32_t>(step),180,80,210,85,1};
        frame.boxes[1]={600-static_cast<int32_t>(step),95,75,160,55,2};
        hv::kernel_protocol::FrameAck ack={};
        result=hv::kernel_client::publish_frame(&client,++sequence,&frame,&ack);
        if(result.code!=hv::kernel_client::Code::Ok) {
            printf("Publish failed at frame %llu (code=%u Win32=%u)\n",
                   static_cast<unsigned long long>(sequence),static_cast<unsigned>(result.code),result.win32_error);
            hv::kernel_client::close(&client);return 3;
        }
        Sleep(33);
    }
    printf("Published %llu bounded, moving test frames in %u seconds.\n",
           static_cast<unsigned long long>(sequence),seconds);
    hv::kernel_client::close(&client);
    return 0;
}
