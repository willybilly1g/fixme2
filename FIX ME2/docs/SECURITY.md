# Security boundaries

- Bez stealth / anticheat / endpoint-security bypass mehanizama.
- Bez stealth input hooks, display hiding, arbitrary kernel read/write, physical RAM exports.
- WDM probe device secured by SYSTEM/Administrators ACL; only version IOCTL.
- Hardware virtualization transitions are intentionally disabled until complete validation.
- Never disable Hyper-V/VBS/HVCI or Windows signing protections just to run this package.
- `HvBoxDemo` je eksplicitan standardan i vidljiv Win32 prozor.
- `PageProvider` never proves physical ownership in user-mode. All physical addresses
  in `HvTests` are synthetic and for deterministic offline emulation only.
- Bugcheck risk exists for any future kernel/VMX implementation. Use isolated test hosts.

## User-mode process memory reader

- `HvProcessManager` uses documented Win32 APIs with OS-enforced `PROCESS_VM_READ`/query permissions; it does not bypass process protection.
- Read-only operations; no arbitrary kernel memory read, no injection and no write operation.
- Name duplicates require explicit PID; maximum single API read is 64KiB, GUI/CLI dump <=256 bytes.
- Process memory may contain private data: use only on processes you own or are authorized to inspect.
