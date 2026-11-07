/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Alexandru Hrisca
 */
/*
 * lilo2 - encoder for the Ricoh SP 211/221 "lilo2" GDI stream.
 * Shared by the CUPS filter (rastertosp211) and the AirPrint server (sp211-airprintd).
 * No CUPS dependency.
 */
#ifndef LILO2_H
#define LILO2_H

#include <stddef.h>
#include <stdint.h>

/* growable byte buffer */
typedef struct {
  unsigned char *d;
  size_t len, cap;
} lilo2_buf;

void lilo2_buf_put(lilo2_buf *b, const void *p, size_t n);
void lilo2_buf_free(lilo2_buf *b);

/* GDIJ / GJET job header */
typedef struct {
  int copies;         /* GDIJ copies field */
  int toner_save;     /* also halves ink values (see lilo2_lut) */
  int edge_smoothing;
  int duplex;         /* manual duplex flag */
  int hires;          /* 1200x600 dpi */
  uint32_t ctime;     /* local time_t of job creation, 0 if unknown */
  const char *title;  /* truncated to 63 bytes */
  const char *user;   /* truncated to 15 bytes */
} lilo2_job;

void lilo2_job_header(lilo2_buf *out, const lilo2_job *job);
void lilo2_job_end(lilo2_buf *out);

/* page flags (GDIP byte 33) */
#define LILO2_PAGE_NORMAL        0x01
#define LILO2_PAGE_DUPLEX_FRONT  0x0d /* even pages, first pass */
#define LILO2_PAGE_DUPLEX_BACK1  0x15 /* first odd page, second pass */
#define LILO2_PAGE_DUPLEX_BACK   0x05

/* paper codes (GDIP byte 8) */
#define LILO2_PAPER_UNSPECIFIED  0xfe

typedef struct {
  /* set by caller */
  unsigned width;         /* pixels per line of the input (ink values) */
  unsigned height;        /* lines */
  unsigned page_w_pt;     /* integer page size in points (as in CUPS PageSize) */
  unsigned page_h_pt;
  int paper, tray, media; /* GDIP codes */
  int flags;              /* LILO2_PAGE_* */
  int pageno;
  int toner_save;
  /* internal */
  lilo2_buf *out;
  unsigned W, obpl, nbands, y;
  unsigned char *band;
  unsigned char lut[256];
} lilo2_page;

/* Writes GDIP; then call lilo2_page_line() height times with 8-bit ink
 * values (0 = white, 255 = black), then lilo2_page_end(). */
int lilo2_page_begin(lilo2_page *pg, lilo2_buf *out);
void lilo2_page_line(lilo2_page *pg, const unsigned char *ink);
void lilo2_page_end(lilo2_page *pg);

/* option keyword -> code helpers (keywords as in the PPD) */
int lilo2_paper_code(const char *keyword);   /* 0 if unknown */
int lilo2_media_code(const char *keyword);   /* 0 if unknown */
int lilo2_tray_code(const char *keyword);    /* 0 if unknown */
int lilo2_duplex_allowed(int paper_code);

#endif
