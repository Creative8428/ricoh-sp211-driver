# Ricoh GDI status protocol (GDIC / GDIR)

How the Ricoh SP 211 reports its state over USB: toner level, paper, page
and misfeed counters. Nothing in this repository uses it yet; the driver and
the AirPrint server only send print data.

**Scope.** Everything here was observed on one printer: Ricoh SP 211,
USB `05ca:0450`, firmware V1.06. The SP 210 shares the Windows driver and
firmware updater, so it very likely behaves the same, but that is untested.

## The SP 211 is not a DDST/PJL printer

The SP 211 identifies itself as `CMD:PDL,PCL,PJL`, but its print stream is the
"lilo" GDI format (`GDIJ`/`GJET`/`GDIP`/`GDIB`/`JIDG` blocks), and it does not
answer PJL status queries over USB. `@PJL INFO`, `INQUIRE`, `DINQUIRE`, `ECHO`
and `USTATUS` were all tried, with and without the `@PJL COMMENT` prefix that
Ricoh's own tool uses; none returned text. In firmware 1.01 the corresponding
handlers read the line and discard it.

## Query and reply

| Direction | Endpoint | Data |
|---|---|---|
| Host → printer | bulk OUT `0x03` | 16 bytes: `"GDIC GDIR       "` |
| Printer → host | bulk IN `0x83` | 64 bytes starting with `"GDIR"` |

- The printer also answers the empty command (`"GDIC"` + 12 spaces) and most
  unrecognised writes with the same packet.
- While it holds a job it sends `GDIR` packets on its own every few seconds.
  CUPS forwards them to filters on the back-channel (fd 3).
- The first query after claiming the interface sometimes gets no reply; retry.

## GDIR packet layout

Multi-byte fields are little-endian.

| Offset | Size | Meaning | How it was established |
|---|---|---|---|
| `0x00` | 4 | `"GDIR"` | capture |
| `0x10` | 4 | pages finished in the current job | firmware + Windows driver |
| `0x1C` | 4 | free image memory (bytes) | firmware; drops per queued job, returns after printing |
| `0x22` | 2 | job held by the printer (0/1) | capture: 0→1 after a job is received, back to 0 once printed; stays 1 while a job is held |
| `0x24` | 1 | status code: 0 normally, 7 in factory mode, nothing else | firmware 1.01 and 1.06: single writer, two values |
| `0x2B` | 1 | bit `0x20`: tray 1 has no paper. Bits `0x40` and `0x10` are always set on the SP 211 and look like the same flag for trays it does not have (tray 2, bypass) | bit `0x20`: capture, 6 transitions; others: inferred from the firmware |
| `0x34` | 1 | black toner remaining, percent | firmware + Windows driver; read 75 |
| `0x35`–`0x37` | 3 | other toner slots (0 on this mono printer) | firmware + Windows driver |

Sources: USB captures on the printer; disassembly of the controller firmware
1.01 and 1.06 (`reversedata.c` packet builder, identical layout in both); disassembly of the Windows driver's
port monitor `gxeg6pcm.dll` (packet parser).

### What "no paper" really means

Bit `0x20` of byte `0x2B`:

- sets when the tray is pulled out;
- sets when a job fails to feed a sheet (about 8 s after the job is sent);
- clears when the tray is pushed back with paper.

An **empty tray that is pushed in is not detected** until a job tries to feed.

After a paper error the printer does not resume by itself: load paper, then
press the **Start** key. The red LED corresponds to "job held + no paper".

## Second channel: vendor registers (page counter)

The printer also answers USB **vendor control requests** on the default
endpoint. This is what the Windows driver uses to feed Ricoh's Smart
Organizing Monitor. It is handled by `vendor_device_request_handler` in the
firmware and is independent of the print data stream.

| Step | bmRequestType | bRequest | wValue | Data |
|---|---|---|---|---|
| prepare | `0xC0` | `0x60` | register number | 2 bytes in (echo of the number) |
| read | `0xC0` | `0x61` | register number | 64 bytes in |
| write | `0x40` | `0x62` | 0 | text out, up to 256 bytes |

The reply is built asynchronously. Until it is ready, "read" returns zeros:
poll every 150 ms until the data starts with `"GDIL"` and byte 4 equals the
register number. Most registers are ready within 150 ms; `0x27` asks the
print engine and takes about 3 s.

> **Do not read register `0x2B`.** On the SP 211 (firmware V1.06) the read
> timed out, the printer dropped off the USB bus and its controller
> rebooted. It came back by itself with counters intact. In the firmware this
> register (model name) runs a diagnostic print with a malformed format
> string. Registers `0x01`-`0x2A` were read without incident; nothing above
> `0x2B` was read.

| Register | Offset | Meaning | How it was established |
|---|---|---|---|
| `0x01` | `0x05` | black toner, percent | equals the GDIR value (75) |
| `0x01` | `0x18`, `0x24` | firmware version strings (`V1.06`) | matches the USB device ID |
| `0x01` | `0x2C` | **total page counter**, 32-bit | went 184 -> 185 after printing one page |
| `0x02` | `0x0C`... | paper dimensions in micrometres (297000 x 210000 = A4) | value |
| `0x03` | `0x3C` | total page counter again | went 184 -> 185 with register 1 |
| `0x10`-`0x23` | | job log, one job per register, cleared at power-on | showed the titles of the CUPS jobs and of the internal test page |
| | `0x06` | job title, 30 bytes | firmware + capture |
| | `0x24` | user name, 20 bytes | firmware (empty in captures) |
| | `0x38` | pages, 16-bit | firmware + capture (1) |
| | `0x3A` | result code 1-6, 16-bit; 2 for jobs that printed | firmware + capture |
| `0x27` | `0x18` | paper misfeeds, total | read 6; maintenance page: 6 |
| `0x27` | `0x1C` | paper misfeeds, tray 1 | read 6; maintenance page: 6 |
| `0x27` | `0x0C`, `0x10`, `0x14`, `0x20` | other misfeed counters (inner, outer, bypass...) | all 0, like the page; which is which is not established |
| `0x27` | `0x2A` | an 11-character alphanumeric identifier | not attributed |

Control requests do not need the printer interface, so the counter can be
read without detaching anything. Whether it can be read while CUPS is in the
middle of a job has not been tried.

Partly understood:

- `0x02` also holds what look like custom paper sizes in 1/100 inch
  (1100 x 850).
- `0x05` and `0x09` contain readable text; in the firmware these fields
  come from the stored access codes. Treat a dump of these registers as
  sensitive and do not publish it.
- `0x24`-`0x26`: firmware and bootloader version strings.

Registers `0x06`-`0x08`, `0x0A`, `0x0C`, `0x28`, `0x29` answer but are not
decoded. The firmware also defines an "engine log" (numbers `0xAB`-`0xAF`);
requests for those numbers were refused by the printer.

### Printing the printer's own pages

The write request `0x62` carries `@SDC_SET` commands (format below). The data
must start with the three bytes `62 FF 00`, which the firmware skips; Ricoh's
tool sends them because it goes through the Windows "vendor set command"
call. Use `bmRequestType 0x41`, `wValue 0xFF00`.

| Data sent | Result on the SP 211 |
|---|---|
| `62 FF 00` + `@SDC_SET08TESTPAGE11GDITESTPAGE` | prints the "Test Page" pattern |
| `62 FF 00` + `@SDC_SET08TESTPAGE14GDIMAINTENANCE` | prints the maintenance page |
| `62 FF 00` + `@SDC_SET08TESTPAGE08SELFTEST` | accepted, nothing printed |
| same text without the three bytes | accepted, nothing printed |
| same text in the print data stream | nothing printed |

Printing starts about 10 s after the request and each page increments the
total counter.

The maintenance page is the cross-check for the values read over USB. On the
test printer it showed `Page Total: 186` (the registers read 186 before it
and 187 after), firmware `V1.06`, bootloader `V1.00` and a cartridge gauge of
4 bars out of 5 (toner read as 75 %). It also lists paper misfeeds (total,
inner, tray 1, bypass, outer) and the engine firmware version; those were not
found in registers `0x01`-`0x26`; the misfeed counters turned out to be in
register `0x27`, the engine firmware version was not found.

## Known gaps

- **Cover open**: no byte changed when the cover was opened while idle.
- **Status code `0x24`**: the Windows driver maps it to 118 messages ("Cover
  Open", jams, service calls...), but this firmware only ever writes 0 or 7
  (factory mode) into it. Those errors are therefore not reported over USB by
  the SP 210/211; this is a firmware limit, not a missing query.
- **`VSTATUS` in the IEEE 1284 device ID** (`IDLE`/`BUSY`/`COMP`/`EROR`):
  `IDLE` → `BUSY` → `IDLE` around a correct job. It stayed `BUSY` for as long
  as the printer held a job with an unknown paper size, and never showed
  `EROR`, even with the red LED on.
- **Red LED after pressing Start on a held job**: nothing changed in the
  packet. Some printer errors are not reported over USB at all.
- **`GDIC CONTINUE`** (resume after paper error): sent with tray/size/type
  bytes `01 09 01`, it had no effect. Format unconfirmed.
- **`GDIC CANCEL`**: the firmware compares the last byte with a job id
  (`0xFF` = any), but returns early unless a job is being received or
  printed. Sent as `"GDIC CANCEL    ÿ"` on a held job, it had no effect.
  A held job was only cleared by power cycling the printer.

### The other channel: `@SDC_SET`

Ricoh's Smart Organizing Monitor changes settings with a text command that
the firmware parses as:

```
@SDC_SET <2-digit name length> <name> <2-digit value length> <value>
```

The firmware table has 183 names. It is write-only (there is no `GET`) and
covers service settings such as `SERIALNO`, fusing temperatures and bias
values. Only the `TESTPAGE` entry was tried (see above); no other setting
was sent to the printer.

The firmware also accepts `GDIC DOWNBOOT`, `DOWNENG`, `DOWNIMAGE` and
`FW TEST`. These are firmware-flashing commands; do not send them.

## Reading the status without blocking printing

The printer sends its `GDIR` packet by itself every 3.5 s as long as the bulk
IN endpoint is being read (measured: 13 packets in 40 s with nothing sent).
Listening to that stream needs the printer interface, which CUPS needs
exclusively to print, so a permanent listener would block printing. A CUPS
filter gets the same packets on its back-channel while its job runs.

The registers do not need the interface, so toner, page counter and misfeeds
can be polled at any time. Paper state is only in the `GDIR` packet. Reading
the registers while a job prints has not been tested.
