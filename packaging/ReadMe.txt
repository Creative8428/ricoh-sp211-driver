NATIVE DRIVER FOR THE RICOH SP 211
==================================

Works on Macs with Apple silicon (M1, M2, M3, M4...) without Rosetta,
so also on macOS 28 and later.

INSTALL
1. Double-click "Install Driver.pkg" and follow the steps.
   You will be asked for your Mac password, as with any driver installer.
2. That's it. If the printer was already set up, it now uses the new driver.
   If it is connected by USB but was not set up, it is added automatically.
3. Print a test page.

IF YOUR MAC REFUSES TO OPEN THE PACKAGE
The package is not signed by Apple, so macOS may say it "cannot be opened
because Apple cannot check it for malicious software".
  - Click "OK" (or "Done").
  - Open System Settings → Privacy & Security.
  - Scroll down and click "Open Anyway" next to the package name.
  - Confirm with your password. The package then starts normally.

ADDING THE PRINTER MANUALLY
System Settings → Printers & Scanners → Add Printer.
Choose the Ricoh SP 211. "Use" should show
"RICOH SP 211 (native Apple Silicon)"; if not, pick it from the list.

TWO-SIDED PRINTING
The printer does not turn the sheet by itself. Choose two-sided printing:
the even pages print first. Put the stack back in the tray, printed side
up, to print the odd pages.

AIRPRINT THROUGH A ROUTER (NO CABLE)
If you have an OpenWrt router with a USB port, connect the printer to it
and double-click "AirPrint on Router.app". Choose "Install", enter the
router address, the SSH port (empty = 22) and the root password. Then:
  - on a Mac: System Settings → Printers & Scanners → Add Printer →
    "RICOH SP 211" (AirPrint);
  - on iPhone/iPad: Share → Print → RICOH SP 211.
To remove it, run the same app and choose "Uninstall".

UNINSTALL
Double-click "Uninstall Driver.app".
Note: Ricoh's original driver needs Rosetta, so it no longer works on macOS 28.

------------------------------------------------------------

DRIVER NATIV PENTRU RICOH SP 211 (Română)
=========================================

Funcționează pe Mac-urile cu procesor Apple (M1, M2, M3, M4...) fără Rosetta,
deci și pe macOS 28 și versiunile următoare.

INSTALARE
1. Dublu-clic pe „Install Driver.pkg” și urmează pașii.
   Ți se cere parola Mac-ului, ca la orice program care instalează drivere.
2. Gata. Dacă imprimanta era deja adăugată, folosește automat driverul nou.
   Dacă e conectată prin USB, dar nu era adăugată, este adăugată automat.
3. Tipărește o pagină de test.

DACĂ MAC-UL REFUZĂ SĂ DESCHIDĂ PACHETUL
Pachetul nu este semnat de Apple, așa că macOS poate afișa un mesaj ca
„nu poate fi deschis deoarece Apple nu poate verifica dacă e malițios”.
  - Apasă „OK” (sau „Gata”).
  - Deschide Configurări sistem → Confidențialitate și securitate.
  - Derulează în jos și apasă „Deschide oricum” lângă numele pachetului.
  - Confirmă cu parola. Pachetul pornește normal.

ADĂUGAREA MANUALĂ A IMPRIMANTEI
Configurări sistem → Imprimante și scanere → Adaugă imprimantă.
Alege imprimanta Ricoh SP 211. La „Utilizare” trebuie să apară
„RICOH SP 211 (native Apple Silicon)”; dacă nu, alege-l din listă.

TIPĂRIRE PE AMBELE FEȚE
Imprimanta nu întoarce singură foaia. Bifează tipărirea pe ambele fețe:
se tipăresc întâi paginile pare. Pune teancul înapoi în tavă, cu fața
tipărită în sus, ca să se tipărească paginile impare.

AIRPRINT PRIN ROUTER (FĂRĂ CABLU)
Dacă ai un router cu OpenWrt care are port USB, conectează imprimanta la el
și dă dublu-clic pe „AirPrint on Router.app”. Alege „Instalează”, scrie adresa
routerului, portul SSH (gol = 22) și parola de root. Apoi:
  - pe Mac: Configurări sistem → Imprimante și scanere → Adaugă imprimantă →
    „RICOH SP 211” (AirPrint);
  - pe iPhone/iPad: Partajare → Tipărire → RICOH SP 211.
Pentru eliminare, rulează aceeași aplicație și alege „Dezinstalează”.

DEZINSTALARE
Dublu-clic pe „Uninstall Driver.app”.
Atenție: driverul Ricoh original are nevoie de Rosetta, deci nu mai merge pe macOS 28.
