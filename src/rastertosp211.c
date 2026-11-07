/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Alexandru Hrisca
 */
/*
 * rastertosp211 - native CUPS filter for Ricoh SP 211 / SP 213 / SP 220 / SP 221
 * class GDI printers (the "lilo2" GDIJ stream format).
 *
 * Drop-in replacement for Ricoh's Intel-only rastertolilo2 filter; the output
 * is byte-identical to it for the supported options. The stream encoder lives
 * in lilo2.c and is shared with the AirPrint server.
 *
 * Usage (as a CUPS filter): rastertosp211 job user title copies options [file]
 */

#include <cups/cups.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "lilo2.h"

static void write_all(const void *p, size_t n) {
  const unsigned char *c = p;
  while (n) {
    ssize_t w = write(1, c, n);
    if (w <= 0) { perror("ERROR: write"); exit(1); }
    c += w; n -= (size_t)w;
  }
}

/* the original filter only accepts "true" (any case) */
static int is_true(const char *v) { return v && !strcasecmp(v, "true"); }

typedef struct {
  int paper, tray, media, toner_save;
} job_opts_t;

/* Reads one raster page (header already read) and appends GDIP + GDIB blocks
 * to out. rotate: 180 degrees (manual duplex back sides, long-edge binding). */
static int do_page(cups_raster_t *ras, cups_page_header2_t *hdr, lilo2_buf *out,
                   const job_opts_t *jo, int tray, int pflags, int pageno, int rotate) {
  unsigned w = hdr->cupsWidth, H = hdr->cupsHeight, bpl = hdr->cupsBytesPerLine;

  if (hdr->cupsBitsPerPixel != 8 || hdr->cupsBitsPerColor != 8) {
    fprintf(stderr, "ERROR: unsupported raster format (%u bpp)\n", hdr->cupsBitsPerPixel);
    return -1;
  }

  lilo2_page pg = {0};
  pg.width = w; pg.height = H;
  pg.page_w_pt = hdr->PageSize[0]; pg.page_h_pt = hdr->PageSize[1];
  pg.paper = jo->paper; pg.tray = tray; pg.media = jo->media;
  pg.flags = pflags; pg.pageno = pageno; pg.toner_save = jo->toner_save;
  if (lilo2_page_begin(&pg, out)) { fputs("ERROR: bad page size\n", stderr); return -1; }

  unsigned char *line = malloc(bpl > w ? bpl : w);
  if (!line) { fputs("ERROR: out of memory\n", stderr); exit(1); }

  if (rotate) {
    unsigned char *page = malloc((size_t)w * H);
    if (!page) { fputs("ERROR: out of memory\n", stderr); exit(1); }
    for (unsigned y = 0; y < H; y++) {
      if (cupsRasterReadPixels(ras, line, bpl) == 0) memset(line, 0, bpl);
      unsigned char *dst = page + (size_t)(H - 1 - y) * w;
      for (unsigned x = 0; x < w; x++) dst[w - 1 - x] = line[x];
    }
    for (unsigned y = 0; y < H; y++) lilo2_page_line(&pg, page + (size_t)y * w);
    free(page);
  } else {
    for (unsigned y = 0; y < H; y++) {
      if (cupsRasterReadPixels(ras, line, bpl) == 0) memset(line, 0, bpl);
      lilo2_page_line(&pg, line);
    }
  }
  lilo2_page_end(&pg);
  free(line);
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 6 || argc > 7) {
    fputs("Usage: rastertosp211 job user title copies options [file]\n", stderr);
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);

  int fd = 0;
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: unable to open raster file");
    return 1;
  }

  cups_option_t *options = NULL;
  int num_options = cupsParseOptions(argv[5], 0, &options);
  const char *v;

  job_opts_t jo;
  v = cupsGetOption("PageSize", num_options, options);
  if (!v) v = cupsGetOption("media", num_options, options);
  jo.paper = v ? lilo2_paper_code(v) : LILO2_PAPER_UNSPECIFIED;
  jo.media = lilo2_media_code(cupsGetOption("MediaType", num_options, options));
  jo.tray = lilo2_tray_code(cupsGetOption("InputSlot", num_options, options));
  jo.toner_save = is_true(cupsGetOption("TonerSave", num_options, options));

  lilo2_job job = {0};
  job.toner_save = jo.toner_save;
  job.edge_smoothing = is_true(cupsGetOption("EdgeSmoothing", num_options, options));
  v = cupsGetOption("Duplex", num_options, options);
  int duplex = v && (!strcmp(v, "DuplexNoTumble") || !strcmp(v, "DuplexTumble")) &&
               lilo2_duplex_allowed(jo.paper);
  int tumble = v && !strcmp(v, "DuplexTumble");
  job.duplex = duplex;
  if ((v = cupsGetOption("time-at-creation", num_options, options)) != NULL) {
    time_t t = (time_t)strtoll(v, NULL, 10);
    struct tm lt;
    localtime_r(&t, &lt);
    job.ctime = (uint32_t)(t + lt.tm_gmtoff);
  }
  job.title = argv[3];
  job.user = argv[2];

  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  if (!ras) { fputs("ERROR: cannot read raster data\n", stderr); return 1; }

  lilo2_buf out = {0};
  lilo2_buf *pages = NULL; /* manual duplex: buffered output for each page */
  int npages = 0, header_done = 0;
  cups_page_header2_t hdr;

  while (cupsRasterReadHeader2(ras, &hdr)) {
    npages++;
    if (!header_done) {
      job.copies = (int)hdr.NumCopies;
      job.hires = hdr.HWResolution[0] >= 1200;
      out.len = 0;
      lilo2_job_header(&out, &job);
      write_all(out.d, out.len);
      header_done = 1;
    }
    fprintf(stderr, "INFO: page %d\n", npages);

    if (!duplex) {
      out.len = 0;
      if (do_page(ras, &hdr, &out, &jo, jo.tray, LILO2_PAGE_NORMAL, npages, 0)) return 1;
      write_all(out.d, out.len);
    } else {
      /* manual duplex: buffer every page; even pages are printed first
       * (rotated for long-edge binding), then the odd pages from Tray 1 */
      pages = realloc(pages, sizeof(lilo2_buf) * npages);
      lilo2_buf *pb = &pages[npages - 1];
      memset(pb, 0, sizeof *pb);
      if (npages % 2 == 0) {
        if (do_page(ras, &hdr, pb, &jo, jo.tray, LILO2_PAGE_DUPLEX_FRONT, npages, !tumble)) return 1;
      } else {
        if (do_page(ras, &hdr, pb, &jo, 2, LILO2_PAGE_DUPLEX_BACK, npages, 0)) return 1;
      }
    }
  }

  if (duplex) {
    for (int i = 2; i <= npages; i += 2) write_all(pages[i - 1].d, pages[i - 1].len);
    for (int i = 1; i <= npages; i += 2) {
      if (i == 1) pages[0].d[33] = LILO2_PAGE_DUPLEX_BACK1;
      write_all(pages[i - 1].d, pages[i - 1].len);
    }
  }

  if (header_done) {
    out.len = 0;
    lilo2_job_end(&out);
    write_all(out.d, out.len);
  }
  cupsRasterClose(ras);
  if (fd) close(fd);
  return npages ? 0 : 1;
}
