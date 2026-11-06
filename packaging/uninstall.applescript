-- Uninstaller app for the native Ricoh SP 211 driver.
-- Runs Contents/Resources/uninstall.sh with administrator privileges.
on run
	set theTitle to "Dezinstalare driver Ricoh SP 211"
	try
		display dialog "Driverul nativ pentru Ricoh SP 211 va fi șters." & return & return & ¬
			"Imprimantele trecute pe el revin la driverul Ricoh original, iar cele adăugate de instalator sunt eliminate." & return & return & ¬
			"Atenție: driverul Ricoh original are nevoie de Rosetta, care nu mai există începând cu macOS 28." ¬
			buttons {"Anulează", "Dezinstalează"} default button "Dezinstalează" cancel button "Anulează" ¬
			with title theTitle with icon caution
	on error number -128
		return
	end try
	set scriptPath to POSIX path of (path to resource "uninstall.sh")
	try
		do shell script "/bin/bash " & quoted form of scriptPath ¬
			with prompt "Introdu parola pentru a dezinstala driverul Ricoh SP 211." with administrator privileges
	on error errMsg number errNum
		if errNum is -128 then return
		display dialog "Dezinstalarea nu a reușit:" & return & errMsg buttons {"OK"} default button "OK" ¬
			with title theTitle with icon stop
		return
	end try
	display dialog "Driverul a fost dezinstalat." buttons {"OK"} default button "OK" with title theTitle with icon note
end run
