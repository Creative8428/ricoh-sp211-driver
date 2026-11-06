-- "AirPrint on Router": installs or removes the Ricoh SP 211 AirPrint server
-- on an OpenWrt router (printer connected to the router's USB port).
-- Texts are in English, or in Romanian when the Mac's language is Romanian.

on isRomanian()
	try
		-- first preferred language, e.g. "ro" from "ro-RO" (not "en-RO")
		set lang to do shell script "defaults read -g AppleLanguages 2>/dev/null | sed -n 2p | tr -d ' \\\",' | cut -c1-2"
		return lang is "ro"
	end try
	return false
end isRomanian

on run
	set ro to isRomanian()
	if ro then
		set lang to "ro"
		set theTitle to "AirPrint pe router – Ricoh SP 211"
		set msgIntro to "Imprimanta Ricoh SP 211 poate fi conectată la portul USB al unui router cu OpenWrt. Routerul o publică în rețea ca imprimantă AirPrint, iar Mac-urile, iPhone-urile și iPad-urile tipăresc prin Wi-Fi, fără driver și fără cablu." & return & return & ¬
			"Ai nevoie de acces SSH la router (de obicei utilizatorul root și parola routerului)."
		set btnCancel to "Anulează"
		set btnRemove to "Dezinstalează"
		set btnInstall to "Instalează"
		set btnContinue to "Continuă"
		set askHost to "Adresa routerului (IP sau nume):"
		set askPort to "Portul SSH al routerului (lasă gol pentru portul standard 22):"
		set msgFail to "Operația nu a reușit."
		set msgInstalled to "Gata! Serverul AirPrint rulează pe router." & return & return & ¬
			"Conectează imprimanta la portul USB al routerului. Pe Mac: Configurări sistem → Imprimante și scanere → Adaugă imprimantă → „RICOH SP 211” (AirPrint). Pe iPhone/iPad apare direct în meniul Tipărire."
		set msgRemoved to "Serverul AirPrint a fost eliminat de pe router."
	else
		set lang to "en"
		set theTitle to "AirPrint on Router – Ricoh SP 211"
		set msgIntro to "The Ricoh SP 211 can be connected to the USB port of an OpenWrt router. The router publishes it on the network as an AirPrint printer, so Macs, iPhones and iPads print over Wi-Fi, with no driver and no cable." & return & return & ¬
			"You need SSH access to the router (usually the root user and the router's password)."
		set btnCancel to "Cancel"
		set btnRemove to "Uninstall"
		set btnInstall to "Install"
		set btnContinue to "Continue"
		set askHost to "Router address (IP or name):"
		set askPort to "Router SSH port (leave empty for the standard port 22):"
		set msgFail to "The operation failed."
		set msgInstalled to "Done! The AirPrint server is running on the router." & return & return & ¬
			"Connect the printer to the router's USB port. On a Mac: System Settings → Printers & Scanners → Add Printer → “RICOH SP 211” (AirPrint). On iPhone/iPad it shows up directly in the Print menu."
		set msgRemoved to "The AirPrint server has been removed from the router."
	end if

	try
		set r to display dialog msgIntro buttons {btnCancel, btnRemove, btnInstall} default button btnInstall ¬
			cancel button btnCancel with title theTitle with icon note
	on error number -128
		return
	end try
	if button returned of r is btnInstall then
		set act to "install"
	else
		set act to "remove"
	end if

	set gw to ""
	try
		set gw to do shell script "route -n get default 2>/dev/null | awk '/gateway/{print $2}'"
	end try
	try
		set hostAnswer to text returned of (display dialog askHost default answer gw with title theTitle ¬
			buttons {btnCancel, btnContinue} default button btnContinue cancel button btnCancel)
		set portAnswer to text returned of (display dialog askPort default answer "" with title theTitle ¬
			buttons {btnCancel, btnContinue} default button btnContinue cancel button btnCancel)
	on error number -128
		return
	end try
	if hostAnswer is "" then return

	set runner to POSIX path of (path to resource "router-run.sh")
	try
		set out to do shell script "SP211_LANG=" & lang & " /bin/bash " & quoted form of runner & " " & act & " " & ¬
			quoted form of hostAnswer & " " & quoted form of portAnswer
	on error errMsg
		display dialog msgFail & return & return & errMsg buttons {"OK"} default button "OK" with title theTitle with icon stop
		return
	end try
	if act is "install" then
		set msg to msgInstalled
	else
		set msg to msgRemoved
	end if
	display dialog msg & return & return & out buttons {"OK"} default button "OK" with title theTitle with icon note
end run
