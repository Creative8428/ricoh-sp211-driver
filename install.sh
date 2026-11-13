#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# Installs the native rastertosp211 filter and switches the printer queue to it.
# Usage: sudo ./install.sh [queue-name]   (default queue: RICOH_SP_211)
set -euo pipefail
[ "$(id -u)" -eq 0 ] || exec sudo "$0" "$@"

HERE=$(cd "$(dirname "$0")" && pwd)
QUEUE=${1:-RICOH_SP_211}
DEST=/Library/Printers/SP211Native
FILTER=$DEST/Filters/rastertosp211
QPPD=/etc/cups/ppd/$QUEUE.ppd

[ -x "$HERE/build/rastertosp211" ] || { echo "Build first: make"; exit 1; }
[ -f "$QPPD" ] || { echo "Queue $QUEUE not found (no $QPPD)"; exit 1; }

install -d -o root -g wheel -m 755 "$DEST/Filters" "$DEST/backup"
install -o root -g wheel -m 755 "$HERE/build/rastertosp211" "$FILTER"
install -o root -g wheel -m 644 "$HERE/LICENSE" "$DEST/LICENSE"   # the license stays next to the filter

# keep the original PPD once, so uninstall.sh can restore it
[ -f "$DEST/backup/$QUEUE.ppd" ] || cp "$QPPD" "$DEST/backup/$QUEUE.ppd"

TMP=$(mktemp)
sed -e "s|^\*cupsFilter:.*|*cupsFilter: \"application/vnd.cups-raster 0 $FILTER\"|" \
    -e 's|^\*NickName: "\(.*\)"|*NickName: "\1 (native Apple Silicon)"|' \
    "$DEST/backup/$QUEUE.ppd" > "$TMP"
grep -q "$FILTER" "$TMP" || { echo "Could not patch PPD"; rm -f "$TMP"; exit 1; }

# also make it selectable when adding the printer again
install -o root -g wheel -m 644 "$TMP" "$DEST/RICOH-SP-211-native.ppd"
gzip -c "$TMP" > "/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"
chmod 644 "/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"

lpadmin -p "$QUEUE" -P "$TMP"
rm -f "$TMP"
echo "Done. Queue $QUEUE now uses $FILTER"
grep '^\*cupsFilter' "$QPPD"
