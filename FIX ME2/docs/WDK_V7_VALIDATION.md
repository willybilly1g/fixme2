# WDK v7 — reproduktivna lokalna validacija

## Preduslovi
- Windows 10 2004+ ili Windows 11 x64, VS2022 + v143 + kompatibilni Windows SDK i WDK.
- Izolovana test masina / VM sa mogucnoscu vracanja snapshot-a.
- Kernel debugger / dump konfiguracija; ne gasiti Hyper-V/VBS/HVCI automatski.
- Drajver mora biti potpisan i instaliran u skladu sa Windows pravilima.

## Build
1. Izvuci ZIP u nov folder (bez starih `out/` fajlova).
2. Otvori `HypervisorLab.sln` u Visual Studio 2022.
3. `Release|x64` ili `Debug|x64`, `Clean Solution`, `Build Solution`.
4. `HvProbeDriver` je namerno izuzet iz podrazumevanog solution build-a.
   Desni klik na projekt > Build; potvrdite WDK target i potpisivanje.
5. `build\build_vs2022.cmd` izvrsava 6 user-mode testova, ne WDK driver test.

## Nakon legitimne instalacije drajvera (na test sistemu)
- Pokreni administrativni `HvKernelProbe.exe` i proveri `Runtime diagnostics`.
- `owned-pages=1`, `phase=1`, `generation>=1`, `IPI diagnostic acks=expected`.
- CPUID bitovi oznacavaju capability, NE aktivnu hardversku virtualizaciju.
- Pokreni `HvGameOverlay.exe --kernel-demo` samo da proveris sinteticki kernel->user box.
- Pokreni `HvGameOverlay.exe --demo` da proveris render bez kernel modula.
- Za stvarni autorizovani process read: normalni `HvGameOverlay.exe`, posebno
  proveri offset-e, verziju EXE/DLL i Windows pristupna prava.
- Ako odgovori ili drajver nisu dostupni, zabelezi Win32 i NTSTATUS kodove.
- Ponovi instaliraj/unload/inicijalizuj ciklus i proveri bez leak-a uz Driver Verifier
  samo na test sistemu sa debugger-om i planom oporavka.

## Release blokatori
VMX/SVM hardware runtime, puna fizicka memorijska infrastruktura, live guest
process-translation kontekst, validiran hardware flush, per-vCPU reclamation,
VMM teardown i real kernel-sourced game telemetry ostaju NOT IMPLEMENTED.
Ne prezentovati IPI rendezvous kao TLB shootdown, ni private page kao arbitrary PMM.
