-- "AirPrint pe router": installs or removes the Ricoh SP 211 AirPrint server
-- on an OpenWrt router (printer connected to the router's USB port).
on run
	set theTitle to "AirPrint pe router – Ricoh SP 211"
	try
		set r to display dialog "Imprimanta Ricoh SP 211 poate fi conectată la portul USB al unui router cu OpenWrt. Routerul o publică în rețea ca imprimantă AirPrint, iar Mac-urile, iPhone-urile și iPad-urile tipăresc prin Wi-Fi, fără driver și fără cablu." & return & return & ¬
			"Ai nevoie de acces SSH la router (de obicei utilizatorul root și parola routerului)." ¬
			buttons {"Anulează", "Dezinstalează", "Instalează"} default button "Instalează" cancel button "Anulează" ¬
			with title theTitle with icon note
	on error number -128
		return
	end try
	if button returned of r is "Instalează" then
		set act to "install"
	else
		set act to "remove"
	end if

	set gw to ""
	try
		set gw to do shell script "route -n get default 2>/dev/null | awk '/gateway/{print $2}'"
	end try
	try
		set hostAnswer to text returned of (display dialog "Adresa routerului (IP sau nume):" default answer gw with title theTitle buttons {"Anulează", "Continuă"} default button "Continuă" cancel button "Anulează")
		set portAnswer to text returned of (display dialog "Portul SSH al routerului (lasă gol pentru portul standard 22):" default answer "" with title theTitle buttons {"Anulează", "Continuă"} default button "Continuă" cancel button "Anulează")
	on error number -128
		return
	end try
	if hostAnswer is "" then return

	set runner to POSIX path of (path to resource "router-run.sh")
	try
		set out to do shell script "/bin/bash " & quoted form of runner & " " & act & " " & quoted form of hostAnswer & " " & quoted form of portAnswer
	on error errMsg
		display dialog "Operația nu a reușit." & return & return & errMsg buttons {"OK"} default button "OK" with title theTitle with icon stop
		return
	end try
	if act is "install" then
		set msg to "Gata! Serverul AirPrint rulează pe router." & return & return & ¬
			"Conectează imprimanta la portul USB al routerului. Pe Mac: Configurări sistem → Imprimante și scanere → Adaugă imprimantă → „RICOH SP 211” (AirPrint). Pe iPhone/iPad apare direct în meniul Tipărire."
	else
		set msg to "Serverul AirPrint a fost eliminat de pe router."
	end if
	display dialog msg & return & return & out buttons {"OK"} default button "OK" with title theTitle with icon note
end run
