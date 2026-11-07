#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# AirPrint server regression test: runs sp211-airprintd locally, sends Apple
# raster and PWG raster pages built from the golden CUPS rasters, and checks
# that the image bands are byte-identical to the original Ricoh output.
# Also runs Apple's IPP 1.1 / 2.0 conformance suites.
set -u
cd "$(dirname "$0")/../.."
make -s build/sp211-airprintd || exit 1
T=$(mktemp -d); PORT=18631
trap 'kill $PID 2>/dev/null; rm -rf "$T"' EXIT
./build/sp211-airprintd -p $PORT -d "$T/out.bin" -s "$T/spool" > "$T/log" 2>&1 & PID=$!
sleep 1
cat > "$T/print.test" <<'X'
{ NAME "Print-Job" OPERATION Print-Job GROUP operation-attributes-tag
  ATTR charset attributes-charset utf-8 ATTR naturalLanguage attributes-natural-language en
  ATTR uri printer-uri $uri ATTR name requesting-user-name tester ATTR name job-name golden
  ATTR mimeMediaType document-format $filetype FILE $filename STATUS successful-ok }
X
pass=0; fail=0
ok() { echo "OK   $1"; pass=$((pass+1)); }; ko() { echo "FAIL $1"; fail=$((fail+1)); }
for c in "text 4961 7016 text" "gray 4961 7016 gray_noise" "letter 5100 6600 letter"; do
  set -- $c
  python3 tests/airprint/mkraster.py tests/golden/$1.ras.gz "$T/$1" $2 $3 >/dev/null
  for x in urf pwg; do
    rm -f "$T/out.bin"; n=$(grep -c printed "$T/log")
    ipptool -q -T 60 -f "$T/$1.$x" ipp://localhost:$PORT/ipp/print "$T/print.test" || { ko "$1.$x (send)"; continue; }
    for i in $(seq 1 120); do [ "$(grep -c printed "$T/log")" -gt "$n" ] && break; sleep 0.5; done
    python3 tests/airprint/compare.py tests/golden/$4.expected "$T/out.bin" >/dev/null && ok "$1.$x bands" || ko "$1.$x bands"
  done
done
for s in ipp-1.1.test ipp-2.0.test; do
  ipptool -t -T 30 -f "$T/text.urf" ipp://localhost:$PORT/ipp/print /usr/share/cups/ipptool/$s > "$T/s.out" 2>&1
  grep -q '\[FAIL\]' "$T/s.out" && ko "$s" || ok "$s"
done
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
