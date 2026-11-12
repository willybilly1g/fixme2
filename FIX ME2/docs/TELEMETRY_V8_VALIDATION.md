# v8 — Windows validation protocol

**Purpose.** Demonstrate a bounded, versioned, authorized producer -> WDK kernel -> user-mode visualization flow. No hypervisor is activated.

## Build on Windows

1. Open `HypervisorLab.sln` in Visual Studio 2022; select `Debug|x64`.
2. Build user-mode solution. Build `HvProbeDriver` separately via project context menu after installing compatible WDK, SDK, MSVC v143 and Spectre-mitigated libraries (MSB8040 prerequisite).
3. Sign/install the WDK driver according to Microsoft's official driver-signing process on a dedicated test machine. Keep VBS/Hyper-V controls untouched.
4. Start an elevated terminal; `HvKernelProbe.exe` should show active kernel *diagnostics*, not VMX/SVM.

## Reproducible visualization checks

1. `HvGameOverlay.exe --moving-demo`: local moving box, no driver.
2. Run `HvGameOverlay.exe --kernel-stream` as administrator. In a second elevated terminal run `HvTelemetryPublisher.exe 30`. Two moving boxes should appear and track the producer's changing coordinates. When it stops, boxes must vanish within 500 ms. For reference, the same source frame is shown via `HvKernelProbe.exe`.
3. Stop/unload the driver, make sure the overlay fails cleanly, reload and repeat. Test invalid frame bounds and version mismatch.
4. OPTIONAL `HvGameOverlay.exe --kernel-game-pipeline`: with an appropriately authorized, running `ourcutegame.exe` containing `ourcutegame.dll` and a window titled `Our Cute Game`, use **existing** Windows `ReadProcessMemory` data and relay it through the driver before rendering. Requires valid version-specific offsets and proper Windows rights; no success claim until observed on a live test system.

## Acceptance checklist

- Moving demo: visible independent object positions, bounded update delay.
- Driver stream: frame IDs monotonic, correct publisher PID, no stale objects after 500 ms.
- No invalid shapes accepted, count <=16, viewport dimensions <=8192.
- No drawing or kernel changes when there is no valid data.
- Build diagnostics, OS/build version, signature verification, Driver Verifier only on isolated test hardware.
- Duplicated simultaneous producer attempts, shutdown under load and process termination should be explicitly tested and reported (NOT TESTED here).

## Architecture boundaries

- EPT/NPT **does not** automatically read Windows host process VA. The existing offline `HvCore` models are not active VMX/SVM.
- The game pipeline uses Windows read-only APIs and never attempts protected-process bypass. The WDK kernel is a bounded relay, not a process scanner.
- All rendering is user-mode GDI using the original draw style.
- Production-ready requires real MSVC/WDK and hardware validation, not merely successful portable unit tests.
