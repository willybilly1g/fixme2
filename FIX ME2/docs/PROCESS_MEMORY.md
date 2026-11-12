# HvProcessManager - Windows process attach and read (C++20, without STL)

## Integration

The solution has new projects:
- `HvProcessManager` - Windows user-mode static library, public header `src/HvProcessManager/include/hv/process.h`.
- `HvProcessInspector` - console app to locate and attach to an EXE by name or PID, inspect its modules, query memory regions, and perform bounded hexadecimal reads.
- `HvProcessTests` - Windows-only tests including read from its own process, no-access rejection, address overflow and boundary checks.
- `HvBoxDemo` - visible GUI that now lets you enter an EXE name or PID and read memory in a bounded hex viewer.

`HvBoxDemo` and `HvProcessInspector` link the same `HvProcessManager` static library.
The `HvProbeDriver` is **not** used by this feature. This is a direct Win32 `OpenProcess`/`ReadProcessMemory` implementation. It does not depend on VMX or SVM execution.

## Build

Install Visual Studio 2022 with C++ desktop workload and the Windows SDK. Open
`HypervisorLab.sln`, select `Release | x64`, Build Solution. The optional WDK
project is excluded from solution-wide build.

Run `build\build_vs2022.cmd` inside an x64 Native Tools Command Prompt to build
and execute available tests, or separately:

```
out\x64\Release\HvProcessTests.exe
out\x64\Release\HvProcessInspector.exe --self-test
```

## GUI workflow

Launch `out\x64\Release\HvBoxDemo.exe`:
1. Start your own test executable (e.g. notepad.exe).
2. Enter `notepad.exe` or its numerical PID in the upper input field.
3. Click **ATTACH**. The process ID and main module base appear in the output.
4. The Address field is prefilled from the main module base. Enter `64` or another count from 1 through 256.
5. Click **REGION** to inspect the address, **READ** to view bytes, or **DETACH** to release the handle.

If two processes have the same EXE name, matching by name returns `AmbiguousName`:
choose one by PID instead. The GUI intentionally does not perform continuous
background reads; every memory read requires a button click.

## Console workflow

```
HvProcessInspector.exe --list
HvProcessInspector.exe --name notepad.exe
HvProcessInspector.exe --pid 1234 --regions 8
HvProcessInspector.exe --pid 1234 --read 0x00007FF700001000 64
HvProcessInspector.exe --pid 1234 --module ntdll.dll
HvProcessInspector.exe --pid 1234 --module ntdll.dll --offset 0x100 --size 64
HvProcessInspector.exe --self-test
```

All example addresses and PIDs are placeholders. A valid address must be obtained
from a live process; ASLR means module bases vary between runs.

## Public C++ API

```
#include "hv/process.h"

hv::process::Target target = {};
uint32_t pid = 0;
auto connected = hv::process::attach_by_name(&target, L"notepad.exe");
if (connected.code == hv::process::Code::Ok) {
    hv::process::Module module = {};
    if (hv::process::main_module(&target, &module).code == hv::process::Code::Ok) {
        uint8_t bytes[64] = {};
        auto r = hv::process::read(&target, module.base, bytes, sizeof(bytes));
        // Check r.code and r.bytes_read. Do not use uninitialized bytes.
    }
    hv::process::detach(&target);
}
```

## Contract and caveats

- **Only allowed reads**. The caller needs normal Windows access rights to the target. `OpenProcess` requests `PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | SYNCHRONIZE`, not `PROCESS_ALL_ACCESS`. Protected processes and cross-user/admin targets may return `AccessDenied`.
- No `SeDebugPrivilege` enabling, no bypass of Protected Process Light, no handle injection, no memory writing, and no kernel memory access.
- A read is restricted to one committed readable `VirtualQueryEx` region and at most 64 KiB by the library. The CLI and GUI deliberately restrict printed dumps to 256 bytes. Large reads should be implemented as validated chunks if required.
- Attach is per PID and owns one process HANDLE. `detach()` closes it. No permanent attachment, injection or suspension of the process is performed.
- The target may exit or change its mappings at any moment. `VirtualQueryEx` provides a point-in-time hint only; `ReadProcessMemory` can still fail. `read()` always returns an explicit status and byte count.
- A remote process may be WOW64; module enumeration uses `TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32` from the x64 tool. Some protected or loader-changing processes still cannot expose their modules reliably.
- `find_pid` and `attach_by_name` return `AmbiguousName` if more than one PID matches (rather than silently attaching to a random instance).
- Production integration should apply its own authorization model: process selection, user consent and audit logging if this will be exposed beyond a local developer tool.
- `HvCore` Intel EPT and AMD NPT remain OFFLINE page-table models. User-mode process reading is independent of, and does not activate, either hypervisor backend.

## Validation state

Source added and integrated into the VS2022 projects; Windows executable tests are included.
This environment is Linux-only and **cannot execute MSVC or Win32 binaries**, so passing the process tests on real Windows is a required outstanding release criterion.
Microsoft API docs:
- https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory
- https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocess
- https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot
