#!/bin/bash
export LANG=C LC_ALL=C   # lpstat output must not be localized
# Uninstaller: restores the original PPD of switched queues, removes queues
# the installer created, and deletes the native filter and PPD.
DEST=/Library/Printers/SP211Native
LOG=/var/log/sp211native-install.log
exec >>"$LOG" 2>&1
echo "== uninstall $(date)"
if [ -d "$DEST/backup" ]; then
  for f in "$DEST"/backup/*.ppd; do
    [ -f "$f" ] || continue
    q=$(basename "$f" .ppd)
    lpstat -p "$q" >/dev/null 2>&1 || continue
    # restore the original PPD only if its filter still exists; otherwise the
    # queue could not print at all, so remove it
    orig=$(sed -n 's|^\*cupsFilter: "[^ ]* [0-9]* \(.*\)"|\1|p' "$f" | head -1)
    if [ -z "$orig" ] || [ -x "$orig" ]; then
      lpadmin -p "$q" -P "$f" && echo "restored $q"
    else
      lpadmin -x "$q" && echo "removed $q (original filter $orig missing)"
    fi
  done
fi
if [ -f "$DEST/created-queues" ]; then
  while read -r q; do [ -n "$q" ] && lpadmin -x "$q" && echo "removed queue $q"; done < "$DEST/created-queues"
fi
rm -f "/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"
rm -rf "$DEST"
pkgutil --forget com.github.creative8428.sp211native >/dev/null 2>&1
exit 0
