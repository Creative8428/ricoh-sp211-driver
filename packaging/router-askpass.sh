#!/bin/bash
# SSH_ASKPASS helper: asks for the router password (or confirms a host key) in a macOS dialog.
prompt="$*"
if [[ "$prompt" == *"yes/no"* ]]; then
  r=$(osascript -e "button returned of (display dialog \"$prompt\" buttons {\"Nu\", \"Da\"} default button \"Da\" with title \"Router\")" 2>/dev/null)
  [ "$r" = "Da" ] && echo yes || echo no
  exit 0
fi
osascript -e 'text returned of (display dialog "Parola routerului (utilizatorul root):" default answer "" with hidden answer buttons {"Anulează", "OK"} default button "OK" cancel button "Anulează" with title "Router")' 2>/dev/null || exit 1
