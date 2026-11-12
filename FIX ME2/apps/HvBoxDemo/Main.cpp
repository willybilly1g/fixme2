#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdint.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include "hv/cpu.h"
#include "hv/process.h"

namespace {
enum : int { id_target = 101, id_attach = 102, id_address = 103,
             id_size = 104, id_read = 105, id_region = 106, id_output = 107,
             id_detach = 108 };

struct GuiState {
    hv::process::Target target;
    HWND target_edit;
    HWND address_edit;
    HWND size_edit;
    HWND output;
    unsigned frame_counter;
    hv::CpuCapabilities caps;
};
GuiState ui = {};

void say(const wchar_t* message) noexcept {
    if (ui.output) SetWindowTextW(ui.output, message);
}

void report(const wchar_t* operation, hv::process::Result result) noexcept {
    wchar_t message[512] = {};
    swprintf_s(message, 512, L"%ls: %hs\r\nWin32 error: %lu\r\nRead: %zu bytes",
               operation, hv::process::code_name(result.code),
               static_cast<unsigned long>(result.system_error), result.bytes_read);
    say(message);
}

bool parse_number(HWND control, uint64_t* value) noexcept {
    wchar_t text[96] = {};
    if (!control || !value || GetWindowTextW(control, text, 96) <= 0 || text[0] == L'-') return false;
    wchar_t* end = nullptr;
    const unsigned long long number = wcstoull(text, &end, 0);
    if (!end || end == text || *end) return false;
    *value = static_cast<uint64_t>(number);
    return true;
}

void attach_target() noexcept {
    wchar_t text[260] = {};
    GetWindowTextW(ui.target_edit, text, 260);
    if (!text[0]) { say(L"Enter an executable name (e.g. notepad.exe) or a PID."); return; }
    uint64_t number = 0;
    wchar_t* end = nullptr;
    number = wcstoull(text, &end, 10);
    uint32_t pid = 0;
    hv::process::Result result = {};
    if (end != text && *end == 0) {
        if (number == 0 || number > UINT32_MAX) { say(L"Invalid PID."); return; }
        pid = static_cast<uint32_t>(number);
    }
    hv::process::detach(&ui.target);
    result = pid ? hv::process::attach(&ui.target, pid)
                 : hv::process::attach_by_name(&ui.target, text);
    if (result.code != hv::process::Code::Ok) { report(L"Attach", result); return; }
    pid = ui.target.pid;
    hv::process::Module module = {};
    result = hv::process::main_module(&ui.target, &module);
    wchar_t message[1024] = {};
    if (result.code == hv::process::Code::Ok) {
        wchar_t address[64] = {};
        swprintf_s(address, 64, L"0x%llX", static_cast<unsigned long long>(module.base));
        SetWindowTextW(ui.address_edit, address);
        swprintf_s(message, 1024,
                   L"ATTACHED (read only)\r\nPID: %lu\r\nMain module: %ls\r\nBase: 0x%016llX\r\nSize: %zu bytes\r\n"
                   L"Choose an address and press READ. Access is controlled by Windows.",
                   pid, module.name, static_cast<unsigned long long>(module.base), module.size);
    } else {
        swprintf_s(message, 1024, L"ATTACHED PID %lu\r\nMain module query: %hs (Win32 %lu)",
                   pid, hv::process::code_name(result.code),
                   static_cast<unsigned long>(result.system_error));
    }
    say(message);
}

void query_region() noexcept {
    if (!hv::process::is_attached(&ui.target)) { say(L"Attach a process first."); return; }
    uint64_t address = 0;
    if (!parse_number(ui.address_edit, &address) || address == 0 || address > UINTPTR_MAX) {
        say(L"Invalid address."); return;
    }
    hv::process::Region region = {};
    hv::process::Result result = hv::process::query_region(&ui.target,
                                                            static_cast<uintptr_t>(address), &region);
    if (result.code != hv::process::Code::Ok) { report(L"VirtualQueryEx", result); return; }
    wchar_t message[512] = {};
    swprintf_s(message, 512,
               L"PID %lu\r\nRegion base: 0x%016llX\r\nRegion bytes: %zu\r\n"
               L"State: 0x%lX\r\nProtection: 0x%lX\r\nType: 0x%lX\r\nReadable: %ls",
               ui.target.pid, static_cast<unsigned long long>(region.base), region.size,
               static_cast<unsigned long>(region.state),
               static_cast<unsigned long>(region.protection),
               static_cast<unsigned long>(region.type), region.readable ? L"YES" : L"NO");
    say(message);
}

void read_address() noexcept {
    if (!hv::process::is_attached(&ui.target)) { say(L"Attach a process first."); return; }
    uint64_t address = 0, size = 0;
    if (!parse_number(ui.address_edit, &address) || !parse_number(ui.size_edit, &size) ||
        address == 0 || address > UINTPTR_MAX || size == 0 || size > 256) {
        say(L"Enter valid address and size (1-256). 0x-prefix accepted."); return;
    }
    unsigned char buffer[256] = {};
    hv::process::Result result = hv::process::read(&ui.target, static_cast<uintptr_t>(address),
                                                   buffer, static_cast<size_t>(size));
    if (result.code != hv::process::Code::Ok) { report(L"ReadProcessMemory", result); return; }
    wchar_t message[4096] = {};
    size_t used = 0;
    int step = swprintf_s(message, 4096, L"PID %lu - read %zu bytes at 0x%llX\r\n\r\n",
                          ui.target.pid, result.bytes_read,
                          static_cast<unsigned long long>(address));
    if (step <= 0) return;
    used = static_cast<size_t>(step);
    for (size_t i = 0; i < result.bytes_read && used + 92 < 4096; i += 16) {
        step = swprintf_s(message + used, 4096 - used, L"%016llX  ",
                          static_cast<unsigned long long>(address + i));
        if (step <= 0) break;
        used += static_cast<size_t>(step);
        for (size_t j = 0; j < 16 && used + 4 < 4096; ++j) {
            if (i + j < result.bytes_read)
                step = swprintf_s(message + used, 4096 - used, L"%02X ", buffer[i + j]);
            else
                step = swprintf_s(message + used, 4096 - used, L"   ");
            if (step <= 0) break;
            used += static_cast<size_t>(step);
        }
        step = swprintf_s(message + used, 4096 - used, L" | ");
        if (step <= 0) break;
        used += static_cast<size_t>(step);
        for (size_t j = 0; j < 16 && i + j < result.bytes_read && used + 2 < 4096; ++j) {
            const unsigned char v = buffer[i + j];
            message[used++] = static_cast<wchar_t>(v >= 32 && v <= 126 ? v : '.');
        }
        message[used++] = L'\r';
        message[used++] = L'\n';
        message[used] = 0;
    }
    say(message);
}

HWND control(HWND parent, const wchar_t* cls, const wchar_t* label, DWORD styles,
             int x, int y, int width, int height, int id) noexcept {
    HWND child = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | styles,
        x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    if (child) SendMessageW(child, WM_SETFONT,
                           reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return child;
}

LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        hv::detect_cpu(&ui.caps);
        control(window, L"STATIC", L"Process name or PID:", 0, 20, 251, 160, 20, 0);
        ui.target_edit = control(window, L"EDIT", L"notepad.exe",
                                 WS_BORDER | ES_AUTOHSCROLL, 180, 248, 275, 26, id_target);
        control(window, L"BUTTON", L"ATTACH", WS_TABSTOP, 465, 247, 90, 28, id_attach);
        control(window, L"BUTTON", L"DETACH", WS_TABSTOP, 565, 247, 90, 28, id_detach);
        control(window, L"STATIC", L"Address:", 0, 20, 288, 150, 20, 0);
        ui.address_edit = control(window, L"EDIT", L"0x0", WS_BORDER | ES_AUTOHSCROLL,
                                  180, 285, 275, 26, id_address);
        control(window, L"STATIC", L"Bytes:", 0, 465, 288, 43, 22, 0);
        ui.size_edit = control(window, L"EDIT", L"64", WS_BORDER | ES_AUTOHSCROLL,
                               511, 285, 50, 26, id_size);
        control(window, L"BUTTON", L"READ", WS_TABSTOP, 570, 285, 90, 28, id_read);
        control(window, L"BUTTON", L"REGION", WS_TABSTOP, 669, 285, 90, 28, id_region);
        ui.output = control(window, L"EDIT",
            L"Choose a process (name.exe or PID) and click ATTACH.\r\n"
            L"The window reads only memory permitted by Windows access controls.\r\n"
            L"Run HvProcessInspector.exe --list to display PIDs.",
            WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
            20, 329, 818, 238, id_output);
        if (ui.output) SendMessageW(ui.output, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
        SetTimer(window, 1, 33, nullptr);
        return 0;
    case WM_TIMER: {
        ++ui.frame_counter;
        RECT top = {0, 0, 860, 238};
        InvalidateRect(window, &top, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED) {
            switch (LOWORD(w)) {
            case id_attach: attach_target(); break;
            case id_detach:
                hv::process::detach(&ui.target);
                say(L"Detached. All process handles closed."); break;
            case id_read: read_address(); break;
            case id_region: query_region(); break;
            default: break;
            }
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        RECT region = {0, 0, 860, 238};
        FillRect(dc, &region, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        const int wobble = static_cast<int>((ui.frame_counter / 10) % 22);
        RECT box = {296-wobble, 45, 565+wobble, 206};
        HBRUSH brush = CreateSolidBrush(RGB(223, 238, 250));
        FillRect(dc, &box, brush);
        DeleteObject(brush);
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(15, 91, 151));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, box.left, box.top, box.right, box.bottom);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(20, 40, 64));
        RECT title = {box.left+16, box.top+22, box.right-16, box.bottom-14};
        DrawTextW(dc, L"WINDOWS TEST BOX\n+ READ-ONLY PROCESS INSPECTOR", -1,
                  &title, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        TextOutW(dc, 20, 20, L"HvBoxDemo - C++20 / VS2022 / no STL", 35);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        hv::process::detach(&ui.target);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, w, l);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"HvLabBoxDemoWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc)) return 1;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"HvBoxDemo - Process Memory Inspector",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 876, 630,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, show);
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
