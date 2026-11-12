# Release gates i prihvatni kriterijumi

## G0 - Scope / Threat model / Windows matrix
**Status: PARTIAL**. Definisan dizajn bez stealth tehnika i bez kernel patchinga.
Za produkciju definisati Windows build, CPU family/model, firmware, microcode,
Hyper-V/VBS/HVCI policy, Secure Boot i driver signing policy; testni host mora biti izolovan.

## G1 - Deterministicki offline core
**Status: PASS za dati portable model**, ne za MSVC na Windowsu.
- Intel EPT / AMD NPT page structure reprezentacija; 2 MiB / 4 KiB.
- Offline 2MiB split, 4KiB protect/query/unmap, atributi PAT/NX gde odgovara.
- No partial publish pri alokacionim neuspesima; validacija stanja pre destroy.
- 707 asertivnih provera; `-fsanitize=address,undefined` i `-Werror` prosli u Linux
  build okruzenju na dan isporuke.
**Dodatno**: pokrenuti iste testove pod MSVC/ASan, clang-cl i Static Analysis.

## G2 - WDK host interface
**Status: NOT TESTED NA WINDOWS-u**.
- Graditi WDK 26100 / MSVC v143 i proveriti IOCTL po ABI ugovoru.
- Code-signing/test signing u odvojenom laboratorijskom sistemu, bez bypass-a.
- Driver Verifier i dump analysis uz WinDbg/Kernel Debugger.
- ACL samo SYSTEM i Administrators; bez pristupa arbitrarnim kernel adresama.

## G3 - Intel VMX/EPT hardware engine
**Status: NOT IMPLEMENTED**.
- VMXON/VMXOFF/VMLAUNCH/VMRESUME kompletan host/guest kontekst.
- Capability MSR-ovi, fixed bits, VMCS control-fields, EPTP, memory type i physical width.
- VM exit entry/exit assembly, CPUID/MSR/interrupt dispatch i RIP policy.
- Single/all-context INVEPT, EPT entry modification ordering.
- Testirati u VM laboratory i zasebno na Intel fizickom test hardveru.

## G4 - AMD SVM/NPT hardware engine
**Status: NOT IMPLEMENTED**.
- EFER.SVME, HSAVE, VMCB, VMRUN/VMLOAD/VMSAVE, ASID resurse.
- SVM intercepts, nested page fault exit codes i host/guest state save/restore.
- VMCB TLB_CONTROL i FlushByASID, kontekstualni NPT flush.
- Ne koristiti INVLPGA kao zamenu za generic NPT GPA invalidaciju.
- SVM test matrix na AMD fizickom test hardveru.

## G5 - Multi-core concurrency i lifecycle
**Status: NOT IMPLEMENTED ZA HARDVER**.
- Windows processor group/topology handling, per-CPU pinned state.
- IPI rendezvous s garantovanim completion i timeout/failure politikom.
- Atomic publication i acquire/release ordering, epoch/refcount reclamation.
- CPU online/offline, power, sleep/resume, cancellation, rollback.
- Ponovljeni start/stop i namerno izazvani neuspesi alokacije na svim fazama.

## G6 - End-to-end i release
**Status: NOT IMPLEMENTED**.
- User <-> kernel verzionisan protokol uz authentikaciju/authorization, rate limit.
- Testirani rendering box i komunikacija bez znacajnog dodatnog jittera.
- BSOD/WHEA/latency counters, VMexit p50/p95/p99, long-run stress, zero unexplained fails.
- Zasebno potpisani Intel i AMD test reportovi, static analysis, SDV gde primenljivo.
- N/A samo uz dokumentovano opravdanje i release-review.

## Statusi
PASS = testirano u eksplicitnom okruzenju; FAIL = kriterijum pao;
NOT TESTED = dokaz ne postoji; PARTIAL = deo zahteva ispunjen.
Ni jedan broj dana bez BSOD-a nije matematicka garancija odsustva bugcheck-a.

## G1.1 - Windows process inspection integration (added)
**Status: SOURCE IMPLEMENTED / WINDOWS BUILD NOT TESTED**.
- Compile `HvProcessManager`, `HvProcessInspector`, `HvProcessTests` and new `HvBoxDemo`.
- Run `HvProcessInspector.exe --self-test`, `HvProcessTests.exe`; require zero failures.
- Confirm attach by name and PID, module lookup and bounded reads from test process.
- Confirm invalid addresses, PAGE_NOACCESS and module instability yield explicit errors.
- Confirm no privilege escalation, process protection bypass or kernel dependencies.
- Check GUI under Windows 10/11 x64 and non-elevated standard account.

## v4 - Process introspection validation

**New development acceptance:** Run `HvIntrospectionTests.exe` on MSVC Debug/Release
x64. A pass validates only offline VA->GPA->HPA fixtures and the transport contract.

For an authorized Windows process, validate that `HvGameOverlay` correctly:
(1) matches game window to process PID and exe name, (2) requires client DLL,
(3) refuses access failures without bypass, (4) reads through `read_batch`,
(5) does not paint stale snapshots after detach, and (6) exits with F9 without
leaked handles. Verify `--demo` with and without the external process.

Gates G3-G6 remain NOT TESTED for physical Intel/AMD virtualization, actual
process CR3 acquisition, second-level invalidation, IPI shootdown, kernel
physical reads, and live multi-core safe reclamation. Do not mark these PASS
from an offline model. See `PROCESS_INTROSPECTION.md`.
