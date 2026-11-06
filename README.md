# Ricoh SP 211 native driver (Apple Silicon, no Rosetta)

**[English](#english)** · **[Română](#română)**

---

## English

Replaces Ricoh's Intel-only `rastertolilo2` filter with `rastertosp211`, built natively for
arm64 (and x86_64), so the printer keeps working on macOS 28 and later, where Rosetta is no
longer available.

The output was verified **byte for byte** against the original driver: dozens of pages and
option combinations (paper sizes, 600 and 1200×600 dpi, toner save, media type, input slot,
manual duplex, titles, copies), including through the full CUPS pipeline (PDF → raster → filter).

### Easy install (no Terminal)
Open `RICOH-SP211-Native-<version>.dmg` and double-click **Install Driver.pkg**.
The installer:
- switches SP 211 printers that are already set up to the new driver;
- adds the printer if it is connected by USB but not set up yet;
- installs its own PPD (`ppd/RICOH-SP-211-native.ppd`), so Ricoh's original driver is no
  longer needed. macOS picks it automatically in *Add Printer*.

To uninstall, use **Uninstall Driver.app** from the same disk image.
Install log: `/var/log/sp211native-install.log`.

The package is not signed or notarized by Apple (that requires an Apple Developer account).
When it was downloaded from the internet, macOS blocks it the first time it is opened. Unblock
it in *System Settings → Privacy & Security → Open Anyway* (details in `Read Me.txt`).

The installer texts are in English, and in Romanian on Macs set to Romanian.

Building the disk image:
```bash
make dmg      # output: dist/RICOH-SP211-Native-<version>.dmg
```

### AirPrint through a router (no cable)
The SP 211 only has USB. Connected to the USB port of an **OpenWrt** router, the printer is
published on the network as an **AirPrint** printer: Macs, iPhones and iPads print over Wi-Fi
without a driver. Tested on a GL.iNet Flint 2 (MT6000) with OpenWrt 25.12.

- **Install / uninstall:** double-click **AirPrint on Router.app** in the disk image. It asks for
  the router address, the SSH port and the root password, copies the server to the router and
  runs `setup.sh install` or `setup.sh remove`.
- **On the router:** `sp211-airprintd` (`src/airprintd.c`), a small IPP server (~180 KB, static,
  no dependencies). It accepts pages as Apple raster (URF) or PWG raster, converts them with the
  same encoder as the CUPS driver (`src/lilo2.c`) and writes them to `/dev/usb/lp0`.
  - Installs `kmod-usb-printer`, the procd service `sp211-airprint`, the Avahi announcement
    and a firewall rule for TCP 631, **from the LAN zone only**.
  - Uninstalling removes all of it, including the kernel module if it installed it.
- **On the Mac:** *System Settings → Printers & Scanners → Add Printer → RICOH SP 211*
  (AirPrint). On iPhone/iPad it shows up directly in the *Print* menu.
- **Verification:** the image bands are byte-identical to the Ricoh driver, and the server passes
  Apple's IPP 1.1/2.0 suites (`./tests/airprint/run.sh`). Duplex is manual, as in the driver.
- **Build:** `make router`, which needs `zig` for cross-compiling
  (`ROUTER_TARGET=aarch64-linux-musl` by default; for other routers e.g. `mipsel-linux-musl`).
  Log on the router: `logread | grep sp211`.
- **OpenWrt upgrades:** the setup survives `sysupgrade` and Attended Sysupgrade when *Keep
  settings* is on. The server, its configuration, the Avahi announcement, the firewall rule and
  the autostart links are listed in `/etc/sysupgrade.conf`, and `kmod-usb-printer` is in
  `/etc/apk/world`, so Attended Sysupgrade builds it into the new image.
  Check with `sysupgrade -l | grep sp211`.

### Install from Terminal
```bash
make
./tests/run_tests.sh      # should print: passed 8, failed 0
sudo ./install.sh         # default queue: RICOH_SP_211
```
The script copies the filter to `/Library/Printers/SP211Native/`, keeps the original PPD and
switches the printer queue to the new filter. The driver also shows up as
"RICOH SP 221, 1.01 (native Apple Silicon)" when you add the printer again.

Back to Ricoh's driver: `sudo ./uninstall.sh`

### What is supported
- All paper sizes in the PPD, 600 dpi and 1200×600 dpi
- Toner Save, Media Type, Input Slot, manual duplex (even pages first, then odd pages)
- Number of copies, job title and user name (shown by the printer)

The hidden options `PBrightness` / `PContrast` (not in the PPD and not in the macOS print dialog)
are not implemented.

### How it works
The "lilo2" format: `GDIJ` and `GJET` blocks (job header), then `GDIP` for each page and `GDIB`
for each 256-line band, JBIG-compressed (T.82), and `JIDG` at the end.
Gray levels are converted with a 24×24 ordered dither matrix measured from the original
driver's output (`src/dither_matrix.h`).

### Components
- `src/rastertosp211.c`: the CUPS filter
- `src/airprintd.c`, `src/lilo2.c`: the AirPrint server and the shared encoder;
  `packaging/router/`: the OpenWrt setup script and service
- `third_party/jbigkit-2.2`: Markus Kuhn's JBIG-KIT library (GPL v2+), with one change in
  `libjbig/jbig.c` (marked `RICOH_FLUSH`): it keeps the trailing zero bytes of each band, exactly
  like Ricoh's encoder. This driver is therefore distributed under the GPL v2+.
- `ppd/`: own PPD for the SP 211 (same options as Ricoh's PPD); `packaging/`: the `.pkg`
  installer, the apps and the disk image
- `research/`: the Python scripts used to decode the format (they need Ricoh's original driver and
  Rosetta; kept as documentation)
- `tests/`: saved rasters and the exact output of the original driver, for tests that also run
  without Rosetta

---

## Română

Înlocuiește filtrul Ricoh `rastertolilo2`, care e doar pentru Intel, cu `rastertosp211`,
compilat nativ pentru arm64 (și x86_64), ca imprimanta să funcționeze și pe macOS 28 și
versiunile următoare, unde Rosetta nu mai există.

Ieșirea a fost verificată **octet cu octet** față de driverul original: zeci de pagini și combinații
de opțiuni (formate de hârtie, 600 și 1200×600 dpi, economie toner, tip hârtie, tavă, duplex manual,
titluri, copii), inclusiv prin lanțul complet CUPS (PDF → raster → filtru).

### Instalare simplă (fără Terminal)
Deschide `RICOH-SP211-Native-<versiune>.dmg` și dă dublu-clic pe **Install Driver.pkg**.
Instalatorul:
- trece automat imprimantele SP 211 deja adăugate pe driverul nou;
- adaugă imprimanta, dacă e conectată prin USB și nu era configurată;
- instalează un PPD propriu (`ppd/RICOH-SP-211-native.ppd`), așa că driverul Ricoh original
  nu mai e necesar. La „Adaugă imprimantă”, macOS îl alege automat.

Pentru dezinstalare există **Uninstall Driver.app** în aceeași imagine.
Jurnalul instalării: `/var/log/sp211native-install.log`.

Pachetul nu e semnat sau notarizat de Apple (pentru asta e nevoie de un cont Apple Developer).
Când e descărcat de pe internet, macOS îl blochează la prima deschidere. Se deblochează din
*Configurări sistem → Confidențialitate și securitate → Deschide oricum* (detalii în `Read Me.txt`).

Textele instalatorului sunt în engleză, iar pe Mac-urile setate în limba română apar în română.

Construirea imaginii DMG:
```bash
make dmg      # rezultat: dist/RICOH-SP211-Native-<versiune>.dmg
```

### AirPrint prin router (fără cablu)
SP 211 are doar USB. Conectată la portul USB al unui router cu **OpenWrt**, imprimanta e publicată
în rețea ca imprimantă **AirPrint**: Mac-urile, iPhone-urile și iPad-urile tipăresc prin Wi-Fi,
fără driver. Testat pe GL.iNet Flint 2 (MT6000), OpenWrt 25.12.

- **Instalare / dezinstalare:** dublu-clic pe **AirPrint on Router.app** din DMG. Ți se cer adresa
  routerului, portul SSH și parola de root. Aplicația copiază serverul pe router și rulează
  `setup.sh install` sau `setup.sh remove`.
- **Pe router:** `sp211-airprintd` (`src/airprintd.c`), un server IPP mic (~180 KB, static,
  fără dependențe). Primește pagini ca Apple raster (URF) sau PWG raster, le convertește cu același
  encoder ca driverul CUPS (`src/lilo2.c`) și le trimite la `/dev/usb/lp0`.
  - Se instalează `kmod-usb-printer`, serviciul procd `sp211-airprint`, anunțul Avahi
    și o regulă de firewall pentru TCP 631, **doar din zona LAN**.
  - Dezinstalarea elimină tot, inclusiv modulul, dacă l-a instalat ea.
- **Pe Mac:** *Configurări sistem → Imprimante și scanere → Adaugă imprimantă → RICOH SP 211*
  (AirPrint). Pe iPhone/iPad apare direct în meniul *Tipărire*.
- **Verificare:** benzile de imagine sunt identice octet cu octet cu driverul Ricoh, iar serverul
  trece suitele Apple IPP 1.1/2.0 (`./tests/airprint/run.sh`). Duplexul e manual, ca în driver.
- **Build:** `make router`, care are nevoie de `zig` pentru compilarea încrucișată
  (`ROUTER_TARGET=aarch64-linux-musl` implicit; pentru alte routere, de ex. `mipsel-linux-musl`).
  Jurnal pe router: `logread | grep sp211`.
- **Upgrade OpenWrt:** configurarea rămâne după `sysupgrade` și după Attended Sysupgrade, dacă
  opțiunea *Keep settings* e bifată. Serverul, configurația lui, anunțul Avahi, regula de firewall
  și legăturile de pornire automată sunt trecute în `/etc/sysupgrade.conf`, iar `kmod-usb-printer`
  e în `/etc/apk/world`, așa că Attended Sysupgrade îl include în imaginea nouă.
  Verificare: `sysupgrade -l | grep sp211`.

### Instalare din Terminal
```bash
make
./tests/run_tests.sh      # trebuie să afișeze: passed 8, failed 0
sudo ./install.sh         # coada implicită: RICOH_SP_211
```
Scriptul copiază filtrul în `/Library/Printers/SP211Native/`, salvează PPD-ul original și
setează coada imprimantei să folosească filtrul nou. Driverul apare și ca
„RICOH SP 221, 1.01 (native Apple Silicon)” când adaugi imprimanta din nou.

Revenire la driverul Ricoh: `sudo ./uninstall.sh`

### Ce e suportat
- Toate formatele de hârtie din PPD, 600 dpi și 1200×600 dpi
- Toner Save, Media Type, Input Slot, Duplex manual (paginile pare întâi, apoi cele impare)
- Numărul de copii, titlul și utilizatorul (afișate de imprimantă)

Opțiunile ascunse `PBrightness` / `PContrast` (nu apar în PPD și nici în dialogul de tipărire
din macOS) nu sunt implementate.

### Cum funcționează
Formatul „lilo2”: blocurile `GDIJ` și `GJET` (antetul jobului), apoi `GDIP` pentru fiecare pagină
și `GDIB` pentru fiecare bandă de 256 de linii, comprimată JBIG (T.82), iar la final `JIDG`.
Nuanțele de gri sunt convertite cu o matrice de dithering ordonat de 24×24, măsurată
din ieșirea driverului original (`src/dither_matrix.h`).

### Componente
- `src/rastertosp211.c`: filtrul CUPS
- `src/airprintd.c`, `src/lilo2.c`: serverul AirPrint și encoderul comun;
  `packaging/router/`: scriptul și serviciul pentru OpenWrt
- `third_party/jbigkit-2.2`: biblioteca JBIG-KIT a lui Markus Kuhn (GPL v2+), cu o singură modificare
  în `libjbig/jbig.c` (marcată `RICOH_FLUSH`): nu elimină octeții 0 de la finalul fiecărei benzi,
  exact ca encoderul Ricoh. Prin urmare, acest driver este distribuit sub GPL v2+.
- `ppd/`: PPD propriu pentru SP 211 (aceleași opțiuni ca PPD-ul Ricoh); `packaging/`: instalatorul
  `.pkg`, aplicațiile și imaginea DMG
- `research/`: scripturile Python folosite la descifrarea formatului (au nevoie de driverul Ricoh
  original și de Rosetta; sunt păstrate ca documentație).
- `tests/`: rastere salvate și rezultatele exacte ale driverului original, pentru teste
  care funcționează și fără Rosetta.
