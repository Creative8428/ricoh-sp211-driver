# Driver nativ pentru Ricoh SP 211 (Apple Silicon, fără Rosetta)

Înlocuiește filtrul Ricoh `rastertolilo2`, care e doar pentru Intel, cu `rastertosp211`,
compilat nativ pentru arm64 (și x86_64). Restul driverului Ricoh (fișierul PPD) rămâne neschimbat.

Ieșirea a fost verificată **octet cu octet** față de driverul original: zeci de pagini și combinații
de opțiuni (formate de hârtie, 600 și 1200×600 dpi, economie toner, tip hârtie, tavă, duplex manual,
titluri, copii), inclusiv prin lanțul complet CUPS (PDF → raster → filtru).

## Instalare
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
- `research/`: scripturile Python folosite la descifrarea formatului (au nevoie de driverul Ricoh
  original și de Rosetta; sunt păstrate ca documentație).
- `tests/`: rastere salvate și rezultatele exacte ale driverului original, pentru teste
  care funcționează și fără Rosetta.
