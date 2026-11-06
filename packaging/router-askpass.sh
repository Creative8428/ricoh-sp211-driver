#!/bin/bash
# SSH_ASKPASS helper: asks for the router password, or confirms the router's
# host key on the first connection, in a macOS dialog.
# SP211_LANG=ro shows Romanian texts, anything else English.
# The text from ssh is passed to AppleScript as an argument, never as code.
prompt="$*"
if [ "${SP211_LANG:-en}" = "ro" ]; then
  yes="Da"; no="Nu"; cancel="Anulează"; ask="Parola routerului (utilizatorul root):"
  hostkey="Prima conexiune la acest router. Verifică dacă amprenta de mai jos este a routerului tău (pe router: ssh-keygen -lf /etc/dropbear/dropbear_ed25519_host_key). Dacă nu ești sigur, apasă Nu."
else
  yes="Yes"; no="No"; cancel="Cancel"; ask="Router password (root user):"
  hostkey="First connection to this router. Check that the fingerprint below belongs to your router (on the router: ssh-keygen -lf /etc/dropbear/dropbear_ed25519_host_key). If you are not sure, click No."
fi

if [[ "$prompt" == *"yes/no"* ]]; then
  r=$(osascript - "$hostkey" "$prompt" "$no" "$yes" <<'APPLESCRIPT' 2>/dev/null
on run argv
	set {intro, details, noBtn, yesBtn} to argv
	button returned of (display dialog intro & return & return & details buttons {noBtn, yesBtn} default button noBtn with title "Router" with icon caution)
end run
APPLESCRIPT
)
  [ "$r" = "$yes" ] && echo yes || echo no
  exit 0
fi

osascript - "$ask" "$cancel" <<'APPLESCRIPT' 2>/dev/null || exit 1
on run argv
	set {askText, cancelBtn} to argv
	text returned of (display dialog askText default answer "" with hidden answer buttons {cancelBtn, "OK"} default button "OK" cancel button cancelBtn with title "Router")
end run
APPLESCRIPT
