# Verification report - v4 (2026-11-12)

This is a development build source release, NOT a tested kernel hypervisor.

- Portable HvCore paging tests: 707 checks / 0 failures (GCC).
- New HvIntrospectionTests: 151 assertions / 0 failures (GCC and Clang).
- New HvIntrospectionTests with Clang ASan+UBSan: 151 / 0 failures.
- HvGameTests geometry: 8 checks / 0 failures (Clang).
- VS2022 project XML and source / project references: 12 / 12 valid.
- Windows 10/11 MSVC compilation: NOT TESTED (Windows toolchain unavailable here).
- Real Our Cute Game exe/dll loading and GDI rendering: NOT TESTED.
- Hardware Intel VMX/AMD SVM activation, IPI, physical memory walk and real CR3: NOT IMPLEMENTED.
- User-mode process integration remains read-only Windows API, within OS access policy.

Target constants: ourcutegame.exe, ourcutegame.dll, Our Cute Game.

Run in VS2022: open HypervisorLab.sln, select Release|x64, build;
run HvTests.exe, HvIntrospectionTests.exe, HvGameTests.exe,
HvGameOverlay.exe --demo, and only then use HvGameOverlay.exe with your
owned/authorized test instance.
