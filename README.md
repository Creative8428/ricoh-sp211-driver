# Driver nativ pentru Ricoh SP 211 (Apple Silicon, fără Rosetta)

Înlocuiește filtrul Ricoh `rastertolilo2`, care e doar pentru Intel, cu `rastertosp211`,
compilat nativ pentru arm64 (și x86_64). Restul driverului Ricoh (fișierul PPD) rămâne neschimbat.

Ieșirea a fost verificată **octet cu octet** față de driverul original: zeci de pagini și combinații
de opțiuni (formate de hârtie, 600 și 1200×600 dpi, economie toner, tip hârtie, tavă, duplex manual,
titluri, copii), inclusiv prin lanțul complet CUPS (PDF → raster → filtru).

## Instalare simplă (fără Terminal)
Deschide `RICOH-SP211-Native-<versiune>.dmg` și dă dublu-clic pe **Instalează driverul.pkg**.
Instalatorul:
- trece automat imprimantele SP 211 deja adăugate pe driverul nou;
- adaugă imprimanta, dacă e conectată prin USB și nu era configurată;
- instalează un PPD propriu (`ppd/RICOH-SP-211-native.ppd`), așa că driverul Ricoh original
  nu mai e necesar. La „Adaugă imprimantă”, macOS îl alege automat.

Pentru dezinstalare există **Dezinstalează driverul.app** în aceeași imagine.
Jurnalul instalării: `/var/log/sp211native-install.log`.

Pachetul nu e semnat sau notarizat de Apple (pentru asta e nevoie de un cont Apple Developer).
Când e descărcat de pe internet, macOS îl blochează la prima deschidere. Se deblochează din
*Configurări sistem → Confidențialitate și securitate → Deschide oricum* (detalii în `Citește-mă.txt`).

Construirea imaginii DMG:
```bash
make dmg      # rezultat: dist/RICOH-SP211-Native-1.1.0.dmg
```

## AirPrint prin router (fără cablu)
SP 211 are doar USB. Conectată la portul USB al unui router cu **OpenWrt**, imprimanta e publicată
în rețea ca imprimantă **AirPrint**: Mac-urile, iPhone-urile și iPad-urile tipăresc prin Wi-Fi,
fără driver. Testat pe GL.iNet Flint 2 (MT6000), OpenWrt 25.12.

- **Instalare / dezinstalare:** dublu-clic pe **AirPrint pe router.app** din DMG. Ți se cer adresa
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

## Instalare din Terminal
```bash
make
./tests/run_tests.sh      # trebuie să afișeze: passed 8, failed 0
sudo ./install.sh         # coada implicită: RICOH_SP_211
```
Scriptul copiază filtrul în `/Library/Printers/SP211Native/`, salvează PPD-ul original și
setează coada imprimantei să folosească filtrul nou. Driverul apare și ca
„RICOH SP 221, 1.01 (native Apple Silicon)” când adaugi imprimanta din nou.

Revenire la driverul Ricoh: `sudo ./uninstall.sh`

## Ce e suportat
- Toate formatele de hârtie din PPD, 600 dpi și 1200×600 dpi
- Toner Save, Media Type, Input Slot, Duplex manual (paginile pare întâi, apoi cele impare)
- Numărul de copii, titlul și utilizatorul (afișate de imprimantă)

Opțiunile ascunse `PBrightness` / `PContrast` (nu apar în PPD și nici în dialogul de tipărire
din macOS) nu sunt implementate.

## Cum funcționează
Formatul „lilo2”: blocurile `GDIJ` și `GJET` (antetul jobului), apoi `GDIP` pentru fiecare pagină
și `GDIB` pentru fiecare bandă de 256 de linii, comprimată JBIG (T.82), iar la final `JIDG`.
Nuanțele de gri sunt convertite cu o matrice de dithering ordonat de 24×24, măsurată
din ieșirea driverului original (`src/dither_matrix.h`).

## Componente
- `src/rastertosp211.c`: filtrul
- `third_party/jbigkit-2.2`: biblioteca JBIG-KIT a lui Markus Kuhn (GPL v2+), cu o singură modificare
  în `libjbig/jbig.c` (marcată `RICOH_FLUSH`): nu elimină octeții 0 de la finalul fiecărei benzi,
  exact ca encoderul Ricoh. Prin urmare, acest filtru este distribuit sub GPL v2+.
- `ppd/`: PPD propriu pentru SP 211 (aceleași opțiuni ca PPD-ul Ricoh); `packaging/`: instalatorul `.pkg` și imaginea DMG
- `src/airprintd.c`, `src/lilo2.c`: serverul AirPrint și encoderul comun; `packaging/router/`: scriptul și serviciul pentru OpenWrt
- `research/`: scripturile Python folosite la descifrarea formatului (au nevoie de driverul Ricoh
  original și de Rosetta; sunt păstrate ca documentație).
- `tests/`: rastere salvate și rezultatele exacte ale driverului original, pentru teste
  care funcționează și fără Rosetta.
