# Our Cute Game — reimplementacija u HypervisorLab v4

**Status:** source integration + testirana prenosiva geometrija. **Nije** hardverski
potvrdjen produkcioni hipervizor ni Windows runtime test.

## Ciljne vrednosti (tacne)

```cpp
#define ENC_OURCUTE_EXE ENC(L"ourcutegame.exe")
#define ENC_CLIENT_DLL ENC(L"ourcutegame.dll")
#define ENC_GAME_WINDOW_TITLE ENC(L"Our Cute Game")
```

Ove konstante su u `apps/HvGameOverlay/include/ourcute/config.h`.
`ENC(text)` je transparentan compatibility macro, bez obfuskacije.

## Da li radi kao original?

`HvGameOverlay` je Windows x64 aplikacija koja koristi `HvIntrospection::read_batch`
preko dozvoljenog `HvProcessManager` Win32 adaptera, bez direktnih
memorijskih Win32 poziva u game logic-u. Poredak provera je **window title → PID iz prozora →
OpenProcess read-only → verifikacija exe imena → enumeration module DLL → scan →
projection → GDI external overlay**. Ako jedan uslov ne prodje, reader ne cita
pogresan proces. Isti vizuelni motiv je reimplementiran: zeleni *corner box* sa
praznim sredinama stranica, bela linija od dna viewporta, pozadina i vertikalni
gradient health bar. `--demo` prikazuje 75% health-box bez igre.

Reimplementirane funkcije:

1. `src/game.cpp`: C API Windows process handle, citanje RAM sa Windows dozvolom,
   jednostruki writer thread, fixed-size SoA snapshot 128 slots, SRWLOCK za publish.
2. `src/geometry.cpp`: projekcija matrice i koordinata, visinska validacija,
   generisanje koordinata corner box-a (testirano nezavisno od Windows-a).
3. `src/render.cpp`: GDI backbuffer, transparent color key, corner lines,
   snapline i prealokirana paleta 256 gradient brushes.
4. `src/main.cpp`: Windows overlay, tracking iskljucivo ciljnog prozora, F9 exit,
   16ms UI timer, `--demo` mode, kontrolisan cleanup.

**Izvor vrednosti offseta:** korisnicki `OurCuteGame2.zip`, fajl
`OurCuteGame2/NewFeetPics/NewFeetPics/src/FeetPics/offsets_local.h` (komentar
originala od 11.11.2026). Taj skup offseta nije validiran na instaliranoj igri.
Ako se verzija igre razlikuje, offsets i layout entiteta zahtevaju zasebnu
proveru. Kod ne tvrdi da se offseti automatski prilagodjavaju novim verzijama.

## Pokretanje u Visual Studio 2022

1. Instalirati Desktop Development with C++, toolset v143 i Windows 10/11 SDK.
2. Otvoriti `HypervisorLab.sln`; `x64 | Release`.
3. Build solution. Driver (`HvProbeDriver`) nije podrazumevano u build-u.
4. `HvTests.exe` → 707 offline checks; `HvGameTests.exe` → 8 geometry checks.
5. `HvGameOverlay.exe --demo` treba da prikaze zeleni corner box, belu snapline
   i gradient health bar. `F9` zatvara aplikaciju.
6. Pokrenuti odgovarajuci `ourcutegame.exe` sa prozorom `Our Cute Game` i
   ucitanim `ourcutegame.dll`.
7. Pokrenuti `HvGameOverlay.exe`. Ako je ciljni prozor aktivan i dostupni su
   dozvoljeni regioni memorije, GUI se pozicionira uz njegov **client rect**.
8. Za proveru modula: `HvProcessInspector.exe --name ourcutegame.exe
   --module ourcutegame.dll` (proveriti `--help` za stvarnu CLI sintaksu).

## Dijagnostika po uzroku

- Ne postoji prozor: overlay se namerno skriva, koristi `--demo`.
- Pogresan PID ili EXE: overlay odbija da cita drugi proces.
- `AccessDenied`: Windows nije odobrio read-only prava. Ne postoji bypass.
- DLL nije ucitan: reader ne uzima modul bazu i ne nastavlja scanning.
- Overlay je prisutan, bez boxeva: najverovatnije zastareli offseti, neuobicajen
  entity layout, nedostupna matrica ili nema validnih jedinica u scenariju.
- Window sizing ne poklapa: proveriti DPI skaliranje, full-screen exclusive
  mode i da li je game window vidljiv/foreground.

## Sta NIJE implementirano

- Nema injekcije DLL-a, presretanja funkcija, stealth overlay-a ni zaobilazenja
  anti-cheat mehanizama. Koriste se samo Windows read-only API-ji.
- `HvCore` ostaje offline EPT/NPT model; **nije** procesni reader i nije aktivna
  VMX/SVM virtualizacija. `INVEPT`, AMD NPT TLB flush, SMP IPI i VM-exit hardver
  nisu implementirani. To je eksplicitan release blocker po v2 specifikaciji.
- Windows/MSVC, test sa stvarnim `ourcutegame.exe` i frame-time benchmark u ovoj
  Linux izvrsnoj okolini nisu dostupni. Ne tvrditi da je igra testirana.

## Testovani delovi

- GCC: HvTests 707/707 PASS; HvGameTests 8/8 PASS.
- Clang: HvGameTests 8/8 PASS.
- MSVC/Win32 GDI/Windows PID+module enumeration: SOURCE ONLY, NOT TESTED.
- Intel VMX/EPT and AMD SVM/NPT execution: NOT IMPLEMENTED.

## v4: memory backend

Vidi `docs/PROCESS_INTROSPECTION.md`. V4 shared introspection API ne tvrdi da
Win32 API i fizicki EPT/NPT page walk implementiraju istu hardversku metodu.
Prvi je autorizovan za zive Windows procese, drugi je testni model, ne runtime.
