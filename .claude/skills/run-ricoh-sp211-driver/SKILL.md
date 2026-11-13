---
name: run-ricoh-sp211-driver
description: Build, run, and drive the Ricoh SP 211 driver without a printer. Use when asked to build it, run or test the CUPS filter (rastertosp211), start the AirPrint server (sp211-airprintd), send it a print job, check its status page, or get a screenshot/preview of a printed page.
---

Two programs share one encoder (`src/lilo2.c`): the CUPS filter `rastertosp211` (a CLI) and the
AirPrint server `sp211-airprintd` (an IPP server). Drive both with
`.claude/skills/run-ricoh-sp211-driver/driver.sh`: it runs the filter on the saved rasters in
`tests/golden`, runs the server on localhost with a file in place of `/dev/usb/lp0`, sends jobs
with `ipptool`, and renders what "came out of the printer" to PNG. No printer, router or `sudo`
is needed. Paths are relative to the repository root. Verified on macOS 27.2 (arm64).

## Prerequisites

- Xcode command line tools: `cc`, `make`, `python3`, and the CUPS tools `ipptool` with its test
  files in `/usr/share/cups/ipptool/`.
- For PNG previews only: `jbgtopbm` at `/opt/homebrew/opt/jbigkit/bin/jbgtopbm` (Homebrew
  package `jbigkit`; it was already installed here). Without it the driver still runs and
  compares, and prints `no preview`.

## Build

```bash
.claude/skills/run-ricoh-sp211-driver/driver.sh build
```

This is `make build/rastertosp211 build/sp211-airprintd`. Plain `make` builds only the filter.

## Run (agent path)

Everything once (build, filter, server, one job per format, status, stop); ends with `SMOKE OK`:

```bash
.claude/skills/run-ricoh-sp211-driver/driver.sh smoke
```

Step by step:

```bash
D=.claude/skills/run-ricoh-sp211-driver/driver.sh
$D filter text "" text                        # filter on tests/golden/text.ras.gz, compare with text.expected
$D filter m5 "TonerSave=True" multi5_toner    # with CUPS options; 5 pages, 5 PNGs
$D serve                                      # AirPrint server on localhost:18640, in the background
$D status                                     # status page, IPP printer state, last log lines
$D print text urf                             # A4 job as Apple raster
$D print letter pwg 5100 6600                 # Letter job as PWG raster (width height in pixels)
$D stop
```

| command | what it does |
|---|---|
| `build` | builds both programs |
| `filter [raster] [options] [expected]` | runs the filter as cupsd would (`job user title copies options file`); `expected` names a `tests/golden/<name>.expected` to compare with |
| `serve [server options]` | starts the server; extra options go to `sp211-airprintd` (e.g. `-v`, `-m 1`) |
| `status` | `GET /` as text, `Get-Printer-Attributes`, last 5 log lines |
| `print [raster] [urf\|pwg] [w h]` | builds the document with `tests/airprint/mkraster.py`, sends `Print-Job`, waits for `printed` in the log |
| `stop` | stops the server |

Rasters: `text`, `gray`, `letter`, `levels1200`, `m4`, `m5`. The comparison names for the filter
are in `tests/run_tests.sh` (for example `gray` → `gray_noise`, `m5` → `multi5`).

Everything lands in `build/run/` (ignored by git):
- `filter-<raster>.lilo2`, `print-<raster>-<fmt>.lilo2`: the printer data stream
- `filter-<raster>-p<N>.png`, `print-<raster>-<fmt>-p<N>.png`: page previews at 1/6 scale. Read
  these to see the page; this is the "screenshot".
- `server.log`, `server.pid`, `printer.bin` (the fake device), `spool/`

A good run prints `bands: 27 vs 27 -> IDENTICAL` for `text`. Header differences listed under it
(`GDIJ differs`, `GDIP differs`) are expected, see Gotchas.

`SP211_PORT` changes the port (default 18640), `SP211_RUN_DIR` the output directory.

## Run (human path)

```bash
./build/sp211-airprintd -p 18640 -d build/run/printer.bin -s build/run/spool
```

Runs in the foreground and logs to the terminal; open `http://localhost:18640/` for the status
page; Ctrl-C stops it. The installers (`sudo ./install.sh`, `make dmg`) and the router build
(`make router`, needs `zig`) change the system or need hardware; they were not run for this skill.

## Test

```bash
./tests/run_tests.sh          # filter, byte for byte: passed 8, failed 0
./tests/airprint/run.sh       # server bands + Apple IPP 1.1/2.0 suites: passed 8, failed 0
./tests/airprint/attack.sh    # ASan/UBSan build under malicious requests: passed 10, failed 0
```

`run.sh` takes about 20 s and `attack.sh` about 6 s; `attack.sh` ends with one more line,
`OK   no AddressSanitizer / UBSan reports`. They use ports 18631 and 18650, so they do
not collide with the driver's server.

## Gotchas

- **Do not `cmp` the filter output with a `.expected` file** unless you reproduce the test's exact
  arguments. The job header (`GDIJ`) holds the title, user and creation time, so
  `rastertosp211 1 tester demo ...` differs from the reference at byte 57 although the page is
  right. `tests/run_tests.sh` passes title `golden` and `TZ=Europe/Luxembourg` for that reason.
  Use `tests/airprint/compare.py`, which requires identical image bands and only lists header
  differences. The driver does this.
- **The server's output never equals the filter's byte for byte.** Its headers carry other paper
  and tray codes (`GDIP differs at 0x8, 0x9`) and a real timestamp. Only the bands must match.
- **A file given with `-d` is appended to**, one job after another (`/dev/...` paths are opened
  as a device instead). Delete it before a job if you want to inspect that job alone; `driver.sh
  print` does, and keeps a copy per job.
- **`Print-Job` returning `[PASS]` only means the job was accepted.** Encoding happens afterwards;
  wait for `job N: printed` in the log before reading the output.
- **`print` needs the full page size in pixels at 600 dpi**: 4961×7016 for the A4 rasters,
  5100×6600 for `letter`. `mkraster.py` places the saved imageable area at the PPD margin.
- **Previews from `m4`/`m5` are small pages** (2304×3296); that is the saved raster, not a bug.
- **A server started with `&` from a script ignores `kill -INT`**, because bash starts background
  jobs with SIGINT ignored; `wait` then hangs forever. Stop it with plain `kill` (SIGTERM), as
  `driver.sh stop` does. Ctrl-C in a terminal works.
- **`tests/airprint/run.sh` prints a `Terminated: 15` line** before its summary when it kills its
  server. Read the last line.

## Troubleshooting

- **`cannot listen on port 18640: Address already in use`** then `error: server did not start`:
  another server is running, usually one started from a different `SP211_RUN_DIR`. Stop it or
  set `SP211_PORT`.
- **`error: already running (pid N)`**: `driver.sh stop` first.
- **`error: not running; run: ... serve`** from `status` or `print`: the server is not started.
- **`error: no tests/golden/<name>.ras.gz`**: the message lists the valid raster names.
