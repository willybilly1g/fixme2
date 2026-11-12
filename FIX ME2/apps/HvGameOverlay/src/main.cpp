#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellscalingapi.h>
#include <string.h>
#include <wchar.h>
#include "ourcute/config.h"
#include "ourcute/game.h"
#include "ourcute/render.h"
#include "hv/kernel_client.h"

namespace {
ourcute::Layer layer = {};
HWND overlay = nullptr;
bool demo = false;
bool kernel_demo = false;
bool kernel_stream = false;
bool kernel_game_pipeline = false;
uint64_t frame_seq = 0;
bool moving_demo = false;
hv::kernel_client::Connection kernel_connection = {};

// The overlay is a documented, external Win32 window: no DLL injection, kernel hook,
// unauthorized VMX/NPT ownership or anti-cheat bypass.
LRESULT CALLBACK overlay_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_TIMER: {
        if(GetAsyncKeyState(VK_F9)&0x8000){DestroyWindow(hwnd);return 0;}
        ourcute::Snapshot snap={};
        if(kernel_stream) {
            hv::kernel_protocol::FrameReply frame = {};
            const auto result=hv::kernel_client::read_frame(&kernel_connection,&frame);
            if(result.code!=hv::kernel_client::Code::Ok) {
                // Always erase old objects if transport is lost or not ready.
                ourcute::layer_draw_frame(&layer,nullptr);
            } else {
                ourcute::layer_draw_frame(&layer,&frame);
            }
            ourcute::layer_present(&layer);
            return 0;
        }
        if(moving_demo) {
            const unsigned pos=static_cast<unsigned>(GetTickCount64()/12ull)%550u;
            const int x=75+static_cast<int>(pos);
            ourcute::layer_draw_kernel_test(&layer,x,155,100,220,75);
            ourcute::layer_present(&layer);
            return 0;
        }
        if(kernel_demo){
            hv::kernel_protocol::RectangleReply sample = {};
            const auto result = hv::kernel_client::rectangle(&kernel_connection,&sample);
            if(result.code != hv::kernel_client::Code::Ok){DestroyWindow(hwnd);return 0;}
            ourcute::layer_draw_kernel_test(&layer,sample.x,sample.y,sample.width,sample.height,
                                            static_cast<int>(sample.health));
            ourcute::layer_present(&layer);
            return 0;
        }
        if(demo){
            snap.attached=true;snap.matrix_valid=true;snap.collected_at_ms=GetTickCount64();
            snap.count=1;snap.view.m[0]=1.0f;snap.view.m[6]=1.0f;snap.view.m[15]=1.0f;
            snap.origin[0]={0,0,-0.5f};snap.eye[0]={0,0,0.5f};snap.health[0]=75;
        } else {
            ourcute::game_snapshot(&snap);
            HWND target=FindWindowW(nullptr,ENC_GAME_WINDOW_TITLE);
            DWORD pid=0;
            if(target)GetWindowThreadProcessId(target,&pid);
            if(!target || !snap.attached || !snap.pid || pid!=snap.pid || IsIconic(target) ||
               GetForegroundWindow()!=target) {
                ShowWindow(hwnd,SW_HIDE);return 0;
            }
            RECT client={};POINT top_left={0,0};
            if(!GetClientRect(target,&client)||!ClientToScreen(target,&top_left) ||
               client.right<=0||client.bottom<=0) {ShowWindow(hwnd,SW_HIDE);return 0;}
            const int width=client.right-client.left, height=client.bottom-client.top;
            if(!ourcute::layer_resize(&layer,width,height)){ShowWindow(hwnd,SW_HIDE);return 0;}
            SetWindowPos(hwnd,HWND_TOPMOST,top_left.x,top_left.y,width,height,SWP_NOACTIVATE);
        }
        if(kernel_game_pipeline) {
            // Explicit Windows read-only source -> fixed payload -> admin WDK relay -> renderer.
            // Neither the driver nor the hypervisor reads game memory here.
            hv::kernel_protocol::FramePayload p = {};
            p.viewport_width = static_cast<uint32_t>(layer.width);
            p.viewport_height = static_cast<uint32_t>(layer.height);
            if(snap.attached && snap.matrix_valid && snap.collected_at_ms &&
               GetTickCount64()-snap.collected_at_ms<=300) {
                for(unsigned i=0;i<snap.count && p.count<hv::kernel_protocol::max_frame_boxes;i++) {
                    ourcute::Box b = {};
                    if(!ourcute::make_box(snap.origin[i],snap.eye[i],&snap.view,
                                          layer.width,layer.height,&b))continue;
                    // Fixed wire format accepts only fully visible, sane rectangles.
                    if(b.left<0 || b.top<0 || b.right>layer.width || b.bottom>layer.height ||
                       b.right-b.left<8 || b.bottom-b.top<8)continue;
                    auto& t=p.boxes[p.count++];
                    t.x=static_cast<int32_t>(b.left);
                    t.y=static_cast<int32_t>(b.top);
                    t.width=static_cast<int32_t>(b.right)-t.x;
                    t.height=static_cast<int32_t>(b.bottom)-t.y;
                    t.health=static_cast<uint32_t>(snap.health[i]);
                    t.object_id=i+1;
                }
            }
            hv::kernel_protocol::FrameAck a = {};
            hv::kernel_protocol::FrameReply rep = {};
            const auto published=hv::kernel_client::publish_frame(&kernel_connection,++frame_seq,&p,&a);
            if(published.code==hv::kernel_client::Code::Ok &&
               hv::kernel_client::read_frame(&kernel_connection,&rep).code==hv::kernel_client::Code::Ok &&
               rep.code==static_cast<uint32_t>(hv::kernel_protocol::Code::Ok) &&
               rep.publisher_pid==GetCurrentProcessId() && rep.frame_sequence==frame_seq) {
                ourcute::layer_draw_frame(&layer,&rep);
            } else {
                ourcute::layer_draw_frame(&layer,nullptr);
            }
        } else {
            ourcute::layer_draw(&layer,&snap,demo);
        }
        ShowWindow(hwnd,SW_SHOWNOACTIVATE);
        ourcute::layer_present(&layer);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps={};BeginPaint(hwnd,&ps);
        ourcute::layer_present(&layer);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_DESTROY:KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int) {
    kernel_game_pipeline=command && wcsstr(command,L"--kernel-game-pipeline");
    kernel_stream=!kernel_game_pipeline && command && wcsstr(command,L"--kernel-stream");
    kernel_demo=!kernel_stream && command && wcsstr(command,L"--kernel-demo");
    moving_demo=!kernel_stream && !kernel_demo && command && wcsstr(command,L"--moving-demo");
    demo=!kernel_stream && !kernel_demo && !moving_demo && command && wcsstr(command,L"--demo");
    if(kernel_demo || kernel_stream || kernel_game_pipeline){
        const auto opened=kernel_game_pipeline ? hv::kernel_client::open_publisher(&kernel_connection) :
                                                hv::kernel_client::open(&kernel_connection);
        if(opened.code != hv::kernel_client::Code::Ok){
            MessageBoxW(nullptr,L"HvLabProbe driver nije dostupan. --kernel-demo zahteva instaliran i potpisan WDK driver.",
                        L"Kernel transport",MB_OK|MB_ICONERROR);return 5;
        }
        hv::kernel_protocol::StatusReply health = {};
        if(hv::kernel_client::status(&kernel_connection,&health).code != hv::kernel_client::Code::Ok){
            hv::kernel_client::close(&kernel_connection);return 6;
        }
    }
    // Attempt per-monitor DPI aware positioning; avoid forcing global display changes.
    typedef BOOL (WINAPI *SetDpiFn)(DPI_AWARENESS_CONTEXT);
    HMODULE user32=GetModuleHandleW(L"user32.dll");
    if(user32){auto f=reinterpret_cast<SetDpiFn>(GetProcAddress(user32,"SetProcessDpiAwarenessContext"));
        if(f)f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);}
    const wchar_t* klass=L"HvGameOverlay_ReadOnly_Window";
    WNDCLASSW wc={};wc.lpfnWndProc=overlay_proc;wc.hInstance=instance;wc.lpszClassName=klass;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc)){hv::kernel_client::close(&kernel_connection);return 2;}
    const bool standalone=(demo||moving_demo||kernel_demo||kernel_stream);
    const DWORD style=standalone?WS_OVERLAPPEDWINDOW:WS_POPUP;
    const DWORD exstyle=standalone?WS_EX_LAYERED:WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
    overlay=CreateWindowExW(exstyle,klass,kernel_stream?L"HvGameOverlay - live admin telemetry":
                            (kernel_demo?L"HvGameOverlay - kernel transport demo":
                            (moving_demo?L"HvGameOverlay - moving geometry demo":
                            (demo?L"HvGameOverlay - demo":L"HvGameOverlay"))),style,
                            standalone?100:0,standalone?100:0,
                            800,600,nullptr,nullptr,instance,nullptr);
    if(!overlay){hv::kernel_client::close(&kernel_connection);UnregisterClassW(klass,instance);return 3;}
    if(!SetLayeredWindowAttributes(overlay,ourcute::transparency_key,0,LWA_COLORKEY) ||
       !ourcute::layer_init(&layer,overlay,800,600)) {
        ourcute::layer_destroy(&layer);DestroyWindow(overlay);hv::kernel_client::close(&kernel_connection);UnregisterClassW(klass,instance);return 4;
    }
    if(!standalone)ourcute::game_start();
    if(standalone)ShowWindow(overlay,SW_SHOW);
    SetTimer(overlay,1,16,nullptr);
    MSG msg={};
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    ourcute::game_stop();
    ourcute::layer_destroy(&layer);
    hv::kernel_client::close(&kernel_connection);
    UnregisterClassW(klass,instance);
    return static_cast<int>(msg.wParam);
}
