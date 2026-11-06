#!/bin/bash
# SSH_ASKPASS helper: asks for the router password (or confirms a host key) in a macOS dialog.
# SP211_LANG=ro shows Romanian texts, anything else English.
prompt="$*"
if [ "${SP211_LANG:-en}" = "ro" ]; then
  yes="Da"; no="Nu"; cancel="Anulează"; ask="Parola routerului (utilizatorul root):"
else
  yes="Yes"; no="No"; cancel="Cancel"; ask="Router password (root user):"
fi
if [[ "$prompt" == *"yes/no"* ]]; then
  r=$(osascript -e "button returned of (display dialog \"$prompt\" buttons {\"$no\", \"$yes\"} default button \"$yes\" with title \"Router\")" 2>/dev/null)
  [ "$r" = "$yes" ] && echo yes || echo no
  exit 0
fi
osascript -e "text returned of (display dialog \"$ask\" default answer \"\" with hidden answer buttons {\"$cancel\", \"OK\"} default button \"OK\" cancel button \"$cancel\" with title \"Router\")" 2>/dev/null || exit 1
