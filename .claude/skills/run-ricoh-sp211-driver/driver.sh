#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# Drives both programs without a printer: the CUPS filter on saved rasters, and
# the AirPrint server on localhost with a file standing in for /dev/usb/lp0.
# State (server pid, log, outputs, previews) lives in build/run, which is ignored by git.
set -u
cd "$(dirname "$0")/../../.."
R=${SP211_RUN_DIR:-build/run}
PORT=${SP211_PORT:-18640}
URI=ipp://localhost:$PORT/ipp/print
JBG=/opt/homebrew/opt/jbigkit/bin/jbgtopbm
mkdir -p "$R"

die() { echo "error: $*" >&2; exit 1; }
running() { [ -f "$R/server.pid" ] && kill -0 "$(cat "$R/server.pid")" 2>/dev/null; }
golden() { [ -f "tests/golden/$1.ras.gz" ] || die "no tests/golden/$1.ras.gz (have: $(ls tests/golden | sed -n 's/\.ras\.gz$//p' | tr '\n' ' '))"; }

# lilo2 stream -> PNG per page (1/6 scale) plus a comparison with the Ricoh reference, if given
show() { # stream base [expected]
  [ -n "${3:-}" ] && python3 tests/airprint/compare.py "$3" "$1"
  if [ -x "$JBG" ]; then python3 tests/airprint/render.py "$1" "$2"
  else echo "no preview: $JBG is missing (brew install jbigkit)"; fi
}

cmd=${1:-help}; [ $# -gt 0 ] && shift
case $cmd in
build)
  make -s build/rastertosp211 build/sp211-airprintd || exit 1
  ls -l build/rastertosp211 build/sp211-airprintd ;;

filter) # filter <raster> [cups options] [expected]: runs the CUPS filter the way cupsd does
  ras=${1:-text}; opts=${2:-}; exp=${3:-}
  golden "$ras"
  [ -x build/rastertosp211 ] || die "run: $0 build"
  gunzip -c "tests/golden/$ras.ras.gz" > "$R/$ras.ras"
  ./build/rastertosp211 1 tester "$ras" 1 "$opts" "$R/$ras.ras" > "$R/filter-$ras.lilo2" 2> "$R/filter-$ras.log" \
    || { cat "$R/filter-$ras.log" >&2; die "filter failed"; }
  cat "$R/filter-$ras.log"
  echo "$R/filter-$ras.lilo2 ($(wc -c < "$R/filter-$ras.lilo2" | tr -d ' ') bytes)"
  [ -n "$exp" ] && exp=tests/golden/$exp.expected
  show "$R/filter-$ras.lilo2" "$R/filter-$ras" "$exp" ;;

serve) # serve [extra sp211-airprintd options, e.g. -v or -m 1]
  running && die "already running (pid $(cat "$R/server.pid")); run: $0 stop"
  [ -x build/sp211-airprintd ] || die "run: $0 build"
  rm -f "$R/printer.bin"
  ./build/sp211-airprintd -p "$PORT" -d "$R/printer.bin" -s "$R/spool" "$@" > "$R/server.log" 2>&1 &
  echo $! > "$R/server.pid"
  for i in 1 2 3 4 5 6 7 8 9 10; do grep -q listening "$R/server.log" && break; sleep 0.2; done
  running || { cat "$R/server.log" >&2; rm -f "$R/server.pid"; die "server did not start"; }
  cat "$R/server.log" ;;

stop)
  running || { rm -f "$R/server.pid"; echo "not running"; exit 0; }
  kill "$(cat "$R/server.pid")"; rm -f "$R/server.pid"; echo "stopped" ;;

status) # the status web page (as text), the printer state over IPP and the end of the log
  running || die "not running; run: $0 serve"
  curl -s "http://localhost:$PORT/" | sed -e 's/<\/t[rh]>/\n/g' -e 's/<[^>]*>/ /g' -e 's/&middot;/-/g' | tr -s ' '
  echo
  ipptool -tv "$URI" get-printer-attributes.test | grep -E '\[(PASS|FAIL)\]|printer-state|queued-job-count|document-format-supported'
  echo "--- $R/server.log"; tail -n 5 "$R/server.log" ;;

print) # print <raster> [urf|pwg] [width height]: sends an AirPrint job and waits for the output
  ras=${1:-text}; fmt=${2:-urf}; w=${3:-4961}; h=${4:-7016}
  golden "$ras"
  running || die "not running; run: $0 serve"
  python3 tests/airprint/mkraster.py "tests/golden/$ras.ras.gz" "$R/$ras" "$w" "$h" || die "mkraster failed"
  cat > "$R/print.test" <<'X'
{ NAME "Print-Job" OPERATION Print-Job GROUP operation-attributes-tag
  ATTR charset attributes-charset utf-8 ATTR naturalLanguage attributes-natural-language en
  ATTR uri printer-uri $uri ATTR name requesting-user-name tester ATTR name job-name golden
  ATTR mimeMediaType document-format $filetype FILE $filename STATUS successful-ok }
X
  # the device file is appended to, so start each job from an empty one
  rm -f "$R/printer.bin"; n=$(grep -c printed "$R/server.log")
  ipptool -t -T 60 -f "$R/$ras.$fmt" "$URI" "$R/print.test" | tail -n 1
  for i in $(seq 1 120); do [ "$(grep -c printed "$R/server.log")" -gt "$n" ] && break; sleep 0.5; done
  [ -s "$R/printer.bin" ] || { tail -n 5 "$R/server.log" >&2; die "nothing was written to $R/printer.bin"; }
  cp "$R/printer.bin" "$R/print-$ras-$fmt.lilo2"
  tail -n 1 "$R/server.log"
  show "$R/print-$ras-$fmt.lilo2" "$R/print-$ras-$fmt" ;;

smoke) # everything once: build, filter, server, one job in each format, stop
  "$0" build && "$0" filter text "" text || exit 1
  "$0" stop >/dev/null; "$0" serve || exit 1
  rc=0
  "$0" print text urf && "$0" print text pwg && "$0" status || rc=1
  "$0" stop
  [ $rc -eq 0 ] && echo "SMOKE OK" || echo "SMOKE FAILED"
  exit $rc ;;

*)
  cat <<X
usage: $0 <command>
  build                                  build the filter and the AirPrint server
  filter [raster] [options] [expected]   run the CUPS filter on tests/golden/<raster>.ras.gz;
                                         compares with tests/golden/<expected>.expected if given
  serve [server options]                 start the AirPrint server on localhost:$PORT
  status                                 status page, IPP printer state, last log lines
  print [raster] [urf|pwg] [w h]         send an AirPrint job (default: text urf, A4 4961x7016)
  stop                                   stop the server
  smoke                                  all of the above, once
Outputs and PNG previews land in $R. Environment: SP211_PORT, SP211_RUN_DIR.
X
  ;;
esac
