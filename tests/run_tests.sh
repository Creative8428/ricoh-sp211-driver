#!/bin/bash
# Regression test: runs the native filter on saved rasters and compares the
# output byte-for-byte with what Ricoh's original filter produced.
# Works without Rosetta or the original driver.
set -u
cd "$(dirname "$0")"
F=../build/rastertosp211
G=golden
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
# golden files were recorded in this time zone (affects time-at-creation)
export TZ=Europe/Luxembourg
pass=0; fail=0
check() { # name raster options
  [ -f "$T/$2" ] || gunzip -c "$G/$2.gz" > "$T/$2"
  "$F" 1 tester golden 1 "$3" "$T/$2" > "$T/out" 2>/dev/null
  if cmp -s "$T/out" "$G/$1.expected"; then echo "OK   $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi
}
check text          text.ras    ""
check gray_noise    gray.ras    ""
check levels1200    levels1200.ras ""
check multi5        m5.ras      ""
check multi5_toner  m5.ras      "TonerSave=True"
check multi5_duplex m5.ras      "Duplex=DuplexNoTumble PageSize=A4 InputSlot=Manual time-at-creation=1762456789"
check multi4_tumble m4.ras      "Duplex=DuplexTumble MediaType=Thick"
check letter        letter.ras  "PageSize=Letter"
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
