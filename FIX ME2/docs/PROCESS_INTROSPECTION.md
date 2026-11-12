# Process Memory Introspection v4 - arhitekturno uskladjivanje

**Status:** implementiran prenosiv, testiran offline page walker i reimplementiran
read-only Windows adapter. **Ne postoji** aktivno VMX/SVM citanje Windows procesa.

## Ispravljena odgovornost slojeva

```text
Ciljni prozor Our Cute Game -> potvrda PID + exe -> modul ourcutegame.dll
                                             |
                                  HvGameOverlay game.cpp
                                             |
                               HvIntrospection::read_batch
                                      /            \
                WindowsVirtual callback          OfflineGuest
                HvProcessManager::read        PML4->PDPT->PD->PT
                dokumentovani Win32 API                 |
                                             GPA -> HvCore::translate
                                                       |
                                           testni host-physical callback
                                             |
                                    iskljucivo validirani snapshot
                                             |
                                    GDI box, snapline, health bar
```

- `HvIntrospection` ne ukljucuje Windows zaglavlja. Ima POD `Context`, callback
  funkcije, jedinstven `Request`, striktno ogranicen batch (<=256 zahteva,
  <=64 KiB), bez STL-a, RTTI, exceptions i alokacije.
- `HvProcessManager::bind_introspection` povezuje vec otvoreni read-only Windows
  `Target` sa `HvIntrospection::Context`, bez eskalacije privilegija.
- `HvGameOverlay::sample` ne poziva `ReadProcessMemory` direktno: svako citanje
  ide kroz `read_batch`; otkrivanje EXE/DLL i prozora ostaje procesni metadata API.
- `bind_hvcore_offline` nudi gotov adapter na `HvCore::translate` za
  testnu fizicku memoriju; `HvIntrospection::resolve_guest_address` vrsi **offline** cetvorostepeni VA->GPA
  walk; za pristup svakoj PT stranici i data stranici koristi second-level
  translator (`HvCore::translate`) i testni callback za fizicki host sadrzaj.
- Podrzani su 4 KiB, 2 MiB i 1 GiB guest leaf entries i validacija canonical
  48-bit adresa, bitova present/user, max physical width i huge-page alignment.
  Intel EPT i AMD NPT su razliciti **second-level backendovi**; nisu format
  guest page tables. LA57 nije implementiran.
- `read_batch` upisuje u korisnicka odredista tek kada sva citanja uspeju i
  generation callback vrati isti token na pocetku i kraju. Pozivalac dostavlja
  scratch buffer. Kod greske nijedan destination se ne menja.

## Vazno: ogranicenja stvarne Windows introspekcije

WindowsVirtual backend koristi `OpenProcess` / `VirtualQueryEx` /
`ReadProcessMemory` koji mogu vratiti AccessDenied. Ne cita fizičku memoriju.
OfflineGuest backend zahteva **unapred dobavljen, autorizovan CR3**,
OS-prikladan host physical-memory backend i sigurnu vCPU izolaciju; trenutno
ima samo fixture/synthetic physical-memory callback. Fizicki EPT/NPT walk
ne znaci automatski da hipervizor ima pristup podacima zivog procesa.

Jednostavan generation counter nije zamena za hardversku koherentnost.
Caller mora obezbediti nepromenljiv snapshot tabelarnih podataka dok traje
jedan batch. Za zivu virtuelnu masinu su i dalje neophodni INVEPT/NPT ASID
flush, IPI ack/quiescence, lifetime management, kontrola CR3 i zastita od
page fault-a ili paged-out memorije. Sve to ostaje NOT IMPLEMENTED i release
blocking prema v2 PDF specifikaciji.

Windows proces moze menjati data pointer-e tokom vise nezavisnih readova.
Win32 backend zato garantuje proveru pristupa i all-or-nothing commit jednog
batch-a, ali **ne** garantuje atomsku fotografiju celokupnog procesa.

## Our Cute Game konfiguracija

```cpp
#define ENC_OURCUTE_EXE ENC(L"ourcutegame.exe")
#define ENC_CLIENT_DLL ENC(L"ourcutegame.dll")
#define ENC_GAME_WINDOW_TITLE ENC(L"Our Cute Game")
```

`HvGameOverlay --demo` prikazuje postojece original-style box i health bar bez
procesa. Bez `--demo` overlay prikazuje rezultate samo kad Windows zaista
dozvoli pristup procesu, kad DLL i offsets odgovaraju, i kad validacija
odgovarajucih struktura uspe. Ne radi DLL injection niti presretanje funkcija.

## Testovi

- `HvIntrospectionTests`: 151 prover(a) u GCC v4 offline fixture-u, EPT i NPT,
  4KiB/2MiB/1GiB VA->GPA->HPA, dva stupnja mapiranja, page crossing, denied
  PTE, noncanonical VA, generation change, request rollback i Windows callback
  kontrakt. MSVC/Windows jos nije testiran.
- `HvTests`: postojeci EPT/NPT offline unit testovi.
- `HvGameTests`: geometrija i projekcija bez Windows zavisnosti.
- Windows manual acceptance: Debug i Release x64 build, inspector, target process
  exe/dll/foreground, frame time i shutdown, samo u autorizovanom test okruzenju.
- Hardware Intel/AMD: G2-G6 iz master PDF-a su i dalje otvoreni.

## Bezbednost i deployment

Bez kernel bypass-a, disable VBS/Hyper-V, editovanja host paging struktura,
stealth renderinga ili nezvanicnog CR3 preuzimanja. `HvProbeDriver` je i dalje
samo read-only dijagnosticki interfejs, ne aktivni hypervisor driver.
