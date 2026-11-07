-- SPDX-License-Identifier: GPL-2.0-or-later
-- Copyright (C) 2026 Alexandru Hrisca
-- Uninstaller app for the native Ricoh SP 211 driver.
-- Runs Contents/Resources/uninstall.sh with administrator privileges.
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
		set theTitle to "Dezinstalare driver Ricoh SP 211"
		set msgIntro to "Driverul nativ pentru Ricoh SP 211 va fi șters." & return & return & ¬
			"Imprimantele trecute pe el revin la driverul Ricoh original, iar cele adăugate de instalator sunt eliminate." & return & return & ¬
			"Atenție: driverul Ricoh original are nevoie de Rosetta, care nu mai există începând cu macOS 28."
		set btnCancel to "Anulează"
		set btnGo to "Dezinstalează"
		set msgPrompt to "Introdu parola pentru a dezinstala driverul Ricoh SP 211."
		set msgFail to "Dezinstalarea nu a reușit:"
		set msgDone to "Driverul a fost dezinstalat."
		set msgTampered to "Aplicația a fost modificată după ce a fost creată și nu va fi rulată. Descarcă din nou imaginea DMG."
	else
		set theTitle to "Uninstall Ricoh SP 211 Driver"
		set msgIntro to "The native driver for the Ricoh SP 211 will be removed." & return & return & ¬
			"Printers that use it go back to Ricoh's original driver, and printers added by the installer are removed." & return & return & ¬
			"Note: Ricoh's original driver needs Rosetta, which is no longer available from macOS 28 on."
		set btnCancel to "Cancel"
		set btnGo to "Uninstall"
		set msgPrompt to "Enter your password to uninstall the Ricoh SP 211 driver."
		set msgFail to "Uninstalling failed:"
		set msgDone to "The driver has been uninstalled."
		set msgTampered to "This app was modified after it was built and will not be run. Download the disk image again."
	end if

	-- refuse to run if the bundle (including its scripts) was modified
	try
		do shell script "/usr/bin/codesign --verify --deep --strict " & quoted form of POSIX path of (path to me)
	on error
		display dialog msgTampered buttons {"OK"} default button "OK" with title theTitle with icon stop
		return
	end try

	try
		display dialog msgIntro buttons {btnCancel, btnGo} default button btnGo cancel button btnCancel ¬
			with title theTitle with icon caution
	on error number -128
		return
	end try
	set scriptPath to POSIX path of (path to resource "uninstall.sh")
	try
		do shell script "/bin/bash " & quoted form of scriptPath with prompt msgPrompt with administrator privileges
	on error errMsg number errNum
		if errNum is -128 then return
		display dialog msgFail & return & errMsg buttons {"OK"} default button "OK" with title theTitle with icon stop
		return
	end try
	display dialog msgDone buttons {"OK"} default button "OK" with title theTitle with icon note
end run
