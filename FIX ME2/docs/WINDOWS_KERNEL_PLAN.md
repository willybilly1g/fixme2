# Windows kernel/VMX/SVM backend engineering plan

**Ovaj tekst nije tvrdnja da je backend implementiran.**

## 1. Architecture boundaries
Zadrzati izvan Win32 UI-a i user-mode CPUID probe koda sve kernel-only
operacije. Ne presretati nelegitimno ownership VMX/SVM od aktivnog Hyper-V/VBS
hypervisora; zabeleziti zauzet hardware i vratiti nesporan status unsupported.
Kreirati WDK-owned IRQL-aware allocator/locking adapter i istinite fizicke adrese.

## 2. Intel engine
Pre aktivacije: VMX MSR capabilities, CR0/CR4 fixed bits, IA32_FEATURE_CONTROL
policy, EPT/INVEPT capability MSRs. Dodeliti VMXON region i VMCS per logical CPU,
proveriti VMCS revision ID, host/guest state, control vectors i EPTP.
U VM-Exit trampoline-u eksplicitno sacuvati sve neophodne registre, FPU/XSTATE
policy, stack i nonvolatile regs. Obnoviti guest state i obraditi VM-entry fail.
INVEPT koristiti samo po hardware capabilities. EPT leaf bitovi, cache type,
A/D i memory aliasing moraju proci CPU-specific proveru.

## 3. AMD engine
Proveriti SVM feature, MSR, eventualno firmware disallow, ASID/NPT capabilities.
Dodeliti VMCB, HSAVE i per CPU stack; `VMRUN`/`VMLOAD`/`VMSAVE` hot path
upravljati precizno, ne deliti Intel VMCS layout. VMCB `TLB_CONTROL` i ASID
semantiku definisati posebno, uz read-only CPU feature snapshots.

## 4. Exit protocol
`ExitEvent` tek kada je realni ASM trampoline implementiran. Nikakvo slepo
inkrementiranje RIP-a. Za CPUID emulator mora obracati paznju na guest feature
mask policy. MSR handler treba allow-list i postupak reinjekcije izuzetaka.
EPT violation, misconfiguration i NPT #NPF imaju razlicite fault decoding puteve.
Nepoznat/nesiguran exit -> deterministicki STOP, ne retry loop.

## 5. Translation invalidation and concurrency
Live map mutation API treba imati internu write lock discipline i stop-the-world
ili dokazano korektnu RCU/epoch politiku. Potreban publish + TLB invalidation
+ potvrda jezgra + deferred free. OfflineGenerationTracker NIJE zamena za IPI.

## 6. Device and application interface
User-mode nikada ne dobija sirove kernel pokazivace niti arbitraran physical mapping.
Svi IOCTL payload-i verzionisani, velicine proverene, autentifikacija i ACL
minimalnih privilegija, cancellation safe, bez METHOD_NEITHER za ovaj proizvod.

## 7. Reliability
Testira se najpre u testnoj VM, potom na izdvojenim masinama; dokumentovati
minidump/kernel dump, WinDbg, Driver Verifier, WHEA, load/unload, sleep/resume,
NUMA/proc group i Windows patch-level promene. Recovery/rollback bez 'BSOD never'.

## Required external manuals
- Intel SDM Vol 3C/3D, VMX, EPT, INVEPT: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
- AMD APM Vol 2 (system programming, SVM): https://www.amd.com/en/search/documentation/hub.html
- Microsoft WDK: https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
- Microsoft Hyper-V conflict guidance: https://learn.microsoft.com/en-us/troubleshoot/windows-client/application-management/virtualization-apps-not-work-with-hyper-v
