/*
 * rastertosp211 - native CUPS filter for Ricoh SP 211 / SP 213 / SP 220 / SP 221
 * class GDI printers (the "lilo2" GDIJ stream format).
 *
 * Drop-in replacement for Ricoh's Intel-only rastertolilo2 filter. The output
 * stream was reconstructed by comparing against the original filter and is
 * byte-identical to it for the supported options.
 *
 * Usage (as a CUPS filter): rastertosp211 job user title copies options [file]
 *
 * Stream layout (all integers big-endian):
 *   GDIJ (120 bytes)  job header: copies, toner save, duplex, time, resolution, title
 *   GJET (168 bytes)  job extension: user name
 *   per page:
 *     GDIP (64 bytes) page header: paper/tray/media codes, size, page number
 *     GDIB (32-byte header + payload) one per 256-line band; payload is a JBIG
 *                     BIE (sequential, LRLTWO, MX=0, L0=256) whose data after
 *                     the 20-byte BIH is zero-padded to a multiple of 32
 *   JIDG (4 bytes)    end of job
 *
 * Halftoning: 24x24 clustered-dot ordered dither (see dither_matrix.h).
 */

#include <cups/cups.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "jbig.h"
#include "dither_matrix.h"

#define BAND_LINES 256

/* ------------------------------------------------------------------ */
/* growable byte buffer                                                */

typedef struct {
  unsigned char *d;
  size_t len, cap;
} buf_t;

static void buf_put(buf_t *b, const void *p, size_t n) {
  if (b->len + n > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 65536;
    while (nc < b->len + n) nc *= 2;
    unsigned char *nd = realloc(b->d, nc);
    if (!nd) { fputs("ERROR: out of memory\n", stderr); exit(1); }
    b->d = nd; b->cap = nc;
  }
  memcpy(b->d + b->len, p, n);
  b->len += n;
}

static void put_be32(unsigned char *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static void put_be16(unsigned char *p, uint16_t v) {
  p[0] = v >> 8; p[1] = v;
}

static void write_all(const void *p, size_t n) {
  const unsigned char *c = p;
  while (n) {
    ssize_t w = write(1, c, n);
    if (w <= 0) { perror("ERROR: write"); exit(1); }
    c += w; n -= (size_t)w;
  }
}

/* ------------------------------------------------------------------ */
/* option -> code tables (values taken from the original filter)       */

typedef struct { const char *name; int code; } code_t;

static const code_t paper_codes[] = {
  {"A4", 9}, {"Letter", 1}, {"B5", 13}, {"B6", 88}, {"A5L", 78}, {"A5", 11},
  {"A6", 70}, {"Legal", 5}, {"Foolscap", 14}, {"Oficio", 132},
  {"IndianLG", 133}, {"AuFOOLSCAP", 134}, {"16K3", 126}, {"HalfLetter", 6},
  {"Executive", 7}, {"Folio", 123}, {"Env10", 20}, {"EnvDL", 27},
  {"EnvC5", 28}, {"EnvMonarch", 37}, {NULL, 0}};

static const code_t media_codes[] = {
  {"PlainRecycled", 0}, {"Plain1", 1}, {"Plain2", 18}, {"Recycled", 2},
  {"Thick", 12}, {"Thick2", 15}, {"MiddleThick", 14}, {"Thin", 13},
  {"Letterhead", 4}, {"Preprinted", 5}, {"Prepunched", 6}, {"Bond", 8},
  {"Cardstock", 9}, {"Color", 3}, {"Label", 7}, {"Envelope", 16},
  {"Special", 19}, {NULL, 0}};

static const code_t tray_codes[] = {
  {"Manual", 1}, {"Tray1", 2}, {"Tray2", 3}, {"Tray3", 4}, {"Tray4", 5},
  {NULL, 0}};

static int lookup(const code_t *t, const char *v, int dflt) {
  if (!v) return dflt;
  for (; t->name; t++)
    if (!strcmp(t->name, v)) return t->code;
  return dflt;
}

/* the original filter only accepts "true" (any case) */
static int is_true(const char *v) { return v && !strcasecmp(v, "true"); }

/* paper codes for which the original filter refuses manual duplex */
static int duplex_allowed(int paper) {
  static const int no_duplex[] = {13, 88, 78, 11, 70, 14, 132, 133, 134, 126, 6, 7};
  for (unsigned i = 0; i < sizeof no_duplex / sizeof *no_duplex; i++)
    if (paper == no_duplex[i]) return 0;
  return 1;
}

/* ------------------------------------------------------------------ */
/* JBIG band encoding                                                  */

static void jbig_out(unsigned char *start, size_t len, void *file) {
  buf_put((buf_t *)file, start, len);
}

/* Encodes one band (W x lines, 1 bpp, MSB first, 1 = black) and appends
 * the complete GDIB block to out. */
static void emit_band(buf_t *out, unsigned char *bits, unsigned W,
                      unsigned lines, uint32_t flag) {
  buf_t bie = {0};
  unsigned char *planes[1] = {bits};
  struct jbg_enc_state s;

  jbg_enc_init(&s, W, lines, 1, planes, jbig_out, &bie);
  jbg_enc_layers(&s, 0);
  jbg_enc_lrange(&s, -1, -1);
  jbg_enc_options(&s, JBG_ILEAVE | JBG_SMID, JBG_LRLTWO, BAND_LINES, 0, -1);
  jbg_enc_out(&s);
  jbg_enc_free(&s);

  /* pad data after the 20-byte BIH to a multiple of 32 */
  static const unsigned char zeros[32];
  size_t pad = (32 - ((bie.len - 20) % 32)) % 32;
  buf_put(&bie, zeros, pad);

  unsigned char h[32] = {'G', 'D', 'I', 'B'};
  put_be32(h + 4, flag);
  put_be32(h + 8, (uint32_t)bie.len);
  put_be16(h + 24, (uint16_t)W);
  put_be16(h + 26, (uint16_t)lines);
  buf_put(out, h, 32);
  buf_put(out, bie.d, bie.len);
  free(bie.d);
}

/* ------------------------------------------------------------------ */

typedef struct {
  int paper, tray, media; /* GDIP codes */
  int toner_save;
} job_opts_t;

/* Reads one raster page (already-read header hdr) and appends GDIP + GDIB
 * blocks to out. rotate: render rotated by 180 degrees (manual duplex back
 * sides). */
static int do_page(cups_raster_t *ras, cups_page_header2_t *hdr, buf_t *out,
                   const job_opts_t *jo, int tray, int pflags, int pageno,
                   int rotate) {
  unsigned w = hdr->cupsWidth, H = hdr->cupsHeight, bpl = hdr->cupsBytesPerLine;
  unsigned W = (w + 127) / 128 * 128;
  unsigned obpl = W / 8;
  unsigned nbands = (H + BAND_LINES - 1) / BAND_LINES;

  if (hdr->cupsBitsPerPixel != 8 || hdr->cupsBitsPerColor != 8) {
    fprintf(stderr, "ERROR: unsupported raster format (%u bpp)\n",
            hdr->cupsBitsPerPixel);
    return -1;
  }

  /* GDIP page header */
  unsigned char p[64] = {'G', 'D', 'I', 'P'};
  put_be32(p + 4, 64);
  p[8] = (unsigned char)jo->paper;
  p[9] = (unsigned char)tray;
  p[10] = (unsigned char)jo->media;
  put_be16(p + 12, (uint16_t)W);
  put_be16(p + 14, (uint16_t)H);
  for (int i = 0; i < 8; i++) put_be16(p + 16 + 2 * i, 100);
  p[32] = 1;
  p[33] = (unsigned char)pflags;
  put_be16(p + 34, (uint16_t)pageno);
  put_be32(p + 52, nbands);
  put_be32(p + 56, (uint32_t)((uint64_t)hdr->PageSize[0] * 25400 / 72));
  put_be32(p + 60, (uint32_t)((uint64_t)hdr->PageSize[1] * 25400 / 72));
  buf_put(out, p, 64);

  unsigned char lut[256];
  for (int v = 0; v < 256; v++) lut[v] = jo->toner_save ? (unsigned char)(v >> 1) : (unsigned char)v;

  unsigned char *line = malloc(bpl);
  unsigned char *band = malloc((size_t)obpl * BAND_LINES);
  unsigned char *page = NULL;
  if (!line || !band) { fputs("ERROR: out of memory\n", stderr); exit(1); }

  if (rotate) {
    /* read the whole page so it can be rotated */
    page = malloc((size_t)w * H);
    if (!page) { fputs("ERROR: out of memory\n", stderr); exit(1); }
    for (unsigned y = 0; y < H; y++) {
      if (cupsRasterReadPixels(ras, line, bpl) == 0) memset(line, 0, bpl);
      unsigned char *dst = page + (size_t)(H - 1 - y) * w;
      for (unsigned x = 0; x < w; x++) dst[w - 1 - x] = line[x];
    }
  }

  for (unsigned b = 0; b < nbands; b++) {
    unsigned y0 = b * BAND_LINES;
    unsigned lines = H - y0 < BAND_LINES ? H - y0 : BAND_LINES;
    memset(band, 0, (size_t)obpl * lines);
    for (unsigned l = 0; l < lines; l++) {
      unsigned y = y0 + l;
      const unsigned char *src;
      if (rotate)
        src = page + (size_t)y * w;
      else {
        if (cupsRasterReadPixels(ras, line, bpl) == 0) memset(line, 0, bpl);
        src = line;
      }
      const unsigned short *t = sp_dither[y % 24];
      unsigned char *o = band + (size_t)l * obpl;
      for (unsigned x = 0; x < w; x++) {
        unsigned v = lut[src[x]];
        if (v && v >= t[x % 24]) o[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
      }
    }
    uint32_t flag = (nbands == 1) ? 3 : (b == 0) ? 1 : (b == nbands - 1) ? 2 : 0;
    emit_band(out, band, W, lines, flag);
  }
  free(line); free(band); free(page);
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
  jo.paper = v ? lookup(paper_codes, v, 0) : 0xfe; /* 0xfe: not specified */
  jo.media = lookup(media_codes, cupsGetOption("MediaType", num_options, options), 0);
  jo.tray = lookup(tray_codes, cupsGetOption("InputSlot", num_options, options), 0);
  jo.toner_save = is_true(cupsGetOption("TonerSave", num_options, options));
  int edge_smoothing = is_true(cupsGetOption("EdgeSmoothing", num_options, options));
  v = cupsGetOption("Duplex", num_options, options);
  int duplex = v && (!strcmp(v, "DuplexNoTumble") || !strcmp(v, "DuplexTumble")) &&
               duplex_allowed(jo.paper);
  int tumble = v && !strcmp(v, "DuplexTumble");
  uint32_t ctime_local = 0;
  if ((v = cupsGetOption("time-at-creation", num_options, options)) != NULL) {
    time_t t = (time_t)strtoll(v, NULL, 10);
    struct tm lt;
    localtime_r(&t, &lt);
    ctime_local = (uint32_t)(t + lt.tm_gmtoff);
  }

  cups_raster_t *ras = cupsRasterOpen(fd, CUPS_RASTER_READ);
  if (!ras) { fputs("ERROR: cannot read raster data\n", stderr); return 1; }

  buf_t out = {0};
  buf_t *back = NULL; /* manual duplex: buffered output for each page */
  int npages = 0, header_done = 0;
  cups_page_header2_t hdr;

  while (cupsRasterReadHeader2(ras, &hdr)) {
    npages++;
    if (!header_done) {
      unsigned char j[120] = {'G', 'D', 'I', 'J'};
      put_be32(j + 4, 120);
      put_be16(j + 8, 100);
      put_be16(j + 10, (uint16_t)hdr.NumCopies);
      j[12] = (unsigned char)jo.toner_save;
      j[13] = (unsigned char)edge_smoothing;
      put_be32(j + 16, (duplex ? 0x04000000u : 0) | 0xa8);
      put_be32(j + 20, ctime_local);
      j[32] = hdr.HWResolution[0] >= 1200 ? 2 : 1;
      j[35] = 2;
      strncpy((char *)j + 56, argv[3], 63);
      write_all(j, sizeof j);

      unsigned char e[168] = {'G', 'J', 'E', 'T'};
      put_be32(e + 4, 168);
      strncpy((char *)e + 72, argv[2], 15);
      write_all(e, sizeof e);
      header_done = 1;
    }
    fprintf(stderr, "INFO: page %d\n", npages);

    if (!duplex) {
      out.len = 0;
      if (do_page(ras, &hdr, &out, &jo, jo.tray, 1, npages, 0)) return 1;
      write_all(out.d, out.len);
    } else {
      /* manual duplex: buffer every page; even pages are printed first
       * (rotated for long-edge binding), then the odd pages */
      back = realloc(back, sizeof(buf_t) * npages);
      buf_t *pb = &back[npages - 1];
      memset(pb, 0, sizeof *pb);
      int even = (npages % 2) == 0;
      if (even) {
        if (do_page(ras, &hdr, pb, &jo, jo.tray, 0x0d, npages, !tumble)) return 1;
      } else {
        /* odd pages: second pass, always from Tray 1; flags patched below */
        if (do_page(ras, &hdr, pb, &jo, 2, 0x05, npages, 0)) return 1;
      }
    }
  }

  if (duplex) {
    for (int i = 2; i <= npages; i += 2) write_all(back[i - 1].d, back[i - 1].len);
    for (int i = 1; i <= npages; i += 2) {
      if (i == 1) back[0].d[33] = 0x15; /* first page of the second pass */
      write_all(back[i - 1].d, back[i - 1].len);
    }
  }

  if (header_done) write_all("JIDG", 4);
  cupsRasterClose(ras);
  if (fd) close(fd);
  return npages ? 0 : 1;
}
