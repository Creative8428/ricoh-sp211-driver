/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lilo2check - decode a printer stream and say what is on each page.
 *
 * A stream with a correct structure can still describe a wrong page (for
 * example an inverted one, which is almost entirely black). This tool decodes
 * the JBIG bands and reports the share of black dots per page, so that a
 * stream can be checked before it is ever sent to a printer.
 *
 *   lilo2check stream.prn            report
 *   lilo2check stream.prn out        also write out-1.pbm, out-2.pbm, ...
 *
 * Exit status: 0 = stream is well formed, 1 = it is not.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jbig.h"

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned long be32(const unsigned char *p) {
  return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

static const unsigned char popcount4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

typedef struct {
  unsigned W, H, paper, tray, media, pageno;
  unsigned long nbands, bands_seen, lines_seen;
  unsigned long long black;
  unsigned char *bits; /* W/8 * H bytes, for the PBM */
} page_t;

static int finish_page(page_t *pg, int index, const char *pbm_base) {
  if (!pg->W) return 0;
  double share = pg->lines_seen ? 100.0 * (double)pg->black / ((double)pg->W * pg->lines_seen) : 0;
  int ok = pg->bands_seen == pg->nbands && pg->lines_seen == pg->H;
  printf("page %d: paper %u tray %u media %u, %ux%u dots, %lu/%lu bands, %lu lines, black %.2f%%%s\n",
         index, pg->paper, pg->tray, pg->media, pg->W, pg->H, pg->bands_seen, pg->nbands,
         pg->lines_seen, share, ok ? "" : "  <-- INCOMPLETE");
  if (pbm_base && pg->bits) {
    char name[512];
    snprintf(name, sizeof name, "%s-%d.pbm", pbm_base, index);
    FILE *f = fopen(name, "wb");
    if (f) {
      fprintf(f, "P4\n%u %u\n", pg->W, pg->H);
      fwrite(pg->bits, pg->W / 8, pg->H, f);
      fclose(f);
    }
  }
  free(pg->bits);
  memset(pg, 0, sizeof *pg);
  return ok ? 0 : 1;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fputs("Usage: lilo2check stream.prn [pbm-basename]\n", stderr);
    return 1;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 1; }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  unsigned char *d = malloc((size_t)size + 1);
  if (!d || fread(d, 1, (size_t)size, f) != (size_t)size) { fputs("read error\n", stderr); return 1; }
  fclose(f);

  const char *pbm_base = argc > 2 ? argv[2] : NULL;
  long i = 0;
  int bad = 0, pages = 0, job_seen = 0, end_seen = 0;
  page_t pg;
  memset(&pg, 0, sizeof pg);

  while (i + 16 <= size && !memcmp(d + i, "GDIC", 4)) i += 16; /* status queries */

  while (i + 4 <= size) {
    const unsigned char *p = d + i;
    if (!memcmp(p, "GDIJ", 4) || !memcmp(p, "GJET", 4)) {
      if (i + 8 > size) { bad = 1; break; }
      if (p[2] == 'I') job_seen = 1;
      i += (long)be32(p + 4);
    } else if (!memcmp(p, "GDIP", 4)) {
      if (i + 64 > size) { bad = 1; break; }
      bad |= finish_page(&pg, pages, pbm_base);
      pages++;
      pg.paper = p[8]; pg.tray = p[9]; pg.media = p[10];
      pg.W = be16(p + 12); pg.H = be16(p + 14);
      pg.pageno = be16(p + 34);
      pg.nbands = be32(p + 52);
      if (!pg.W || pg.W % 8 || !pg.H) { printf("page %d: bad size %ux%u\n", pages, pg.W, pg.H); bad = 1; break; }
      pg.bits = calloc(pg.W / 8, pg.H);
      i += 64;
    } else if (!memcmp(p, "GDIB", 4)) {
      if (i + 32 > size || !pg.W) { bad = 1; break; }
      unsigned long len = be32(p + 8);
      unsigned W = be16(p + 24), lines = be16(p + 26);
      if (i + 32 + (long)len > size) { printf("band runs past the end of the file\n"); bad = 1; break; }
      struct jbg_dec_state s;
      size_t used = 0;
      jbg_dec_init(&s);
      int r = jbg_dec_in(&s, (unsigned char *)p + 32, len, &used);
      if (r != JBG_EOK && r != JBG_EOK_INTR) {
        printf("page %d band %lu: JBIG error %s\n", pages, pg.bands_seen + 1, jbg_strerror(r));
        bad = 1;
      } else if (jbg_dec_getwidth(&s) != W || jbg_dec_getheight(&s) != lines || W != pg.W) {
        printf("page %d band %lu: size %lux%lu, header says %ux%u\n", pages, pg.bands_seen + 1,
               jbg_dec_getwidth(&s), jbg_dec_getheight(&s), W, lines);
        bad = 1;
      } else {
        const unsigned char *img = jbg_dec_getimage(&s, 0);
        size_t n = (size_t)(W / 8) * lines;
        for (size_t k = 0; k < n; k++) pg.black += popcount4[img[k] >> 4] + popcount4[img[k] & 15];
        if (pg.bits && pg.lines_seen + lines <= pg.H)
          memcpy(pg.bits + (size_t)(W / 8) * pg.lines_seen, img, n);
      }
      jbg_dec_free(&s);
      pg.bands_seen++;
      pg.lines_seen += lines;
      i += 32 + (long)len;
    } else if (!memcmp(p, "JIDG", 4)) {
      end_seen = 1;
      i += 4;
      break;
    } else {
      printf("unknown block at offset %ld\n", i);
      bad = 1;
      break;
    }
  }
  bad |= finish_page(&pg, pages, pbm_base);
  if (!job_seen) { puts("no job header (GDIJ)"); bad = 1; }
  if (!end_seen) { puts("no end of job (JIDG)"); bad = 1; }
  if (i != size) { printf("%ld bytes after the end of job\n", size - i); bad = 1; }
  printf("%d page(s), %s\n", pages, bad ? "STREAM IS NOT WELL FORMED" : "stream is well formed");
  free(d);
  return bad;
}
