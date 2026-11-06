#!/bin/bash
# Restores the original Ricoh PPD (and filter) for the queue and removes the native filter.
# Usage: sudo ./uninstall.sh [queue-name]
set -euo pipefail
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"
QUEUE=${1:-RICOH_SP_211}
DEST=/Library/Printers/SP211Native
if [ -f "$DEST/backup/$QUEUE.ppd" ]; then
  lpadmin -p "$QUEUE" -P "$DEST/backup/$QUEUE.ppd"
  echo "Restored original PPD for $QUEUE"
fi
rm -f "/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"
rm -rf "$DEST"
echo "Native filter removed."
