#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# Security regression test: builds sp211-airprintd with AddressSanitizer, sends
# malicious requests (tests/airprint/attack.py) and fails on any memory error.
set -u
cd "$(dirname "$0")/../.."
T=$(mktemp -d); PORT=18650
J=third_party/jbigkit-2.2/libjbig
cc -g -O1 -fsanitize=address,undefined -Isrc -I$J -o "$T/srv" src/airprintd.c src/lilo2.c $J/jbig.c $J/jbig_ar.c -lpthread 2>/dev/null || exit 1
ASAN_OPTIONS=detect_leaks=0 "$T/srv" -p $PORT -d "$T/out" -s "$T/spool" -m 1 > "$T/log" 2>&1 & PID=$!
sleep 1
python3 tests/airprint/attack.py $PORT; rc=$?
kill $PID 2>/dev/null; wait $PID 2>/dev/null
if grep -qE "AddressSanitizer|runtime error" "$T/log"; then
  echo "FAIL memory/UB errors:"; grep -E "ERROR|runtime error|SUMMARY" "$T/log" | head; rc=1
else
  echo "OK   no AddressSanitizer / UBSan reports"
fi
rm -rf "$T"
exit $rc
