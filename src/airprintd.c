/*
 * sp211-airprintd - minimal IPP Everywhere / AirPrint server for the Ricoh SP 211.
 *
 * Runs on a small always-on host (e.g. an OpenWrt router) with the printer on
 * USB. Clients (macOS, iOS, iPadOS) discover it via DNS-SD (published by avahi)
 * and send pages already rasterised as Apple raster (image/urf) or PWG raster
 * (image/pwg-raster). Pages are converted with the shared lilo2 encoder and
 * written to the printer device (e.g. /dev/usb/lp0).
 *
 * Supported IPP operations: Print-Job, Validate-Job, Create-Job, Send-Document,
 * Cancel-Job, Get-Job-Attributes, Get-Jobs, Get-Printer-Attributes, Close-Job,
 * Identify-Printer.
 *
 * Usage: sp211-airprintd [-p port] [-d device] [-s spooldir] [-n name]
 *                        [-u uuid] [-l listen-addr] [-v]
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lilo2.h"
#include "icon_png.h"

/* ------------------------------------------------------------------ */
/* configuration                                                       */

static int cfg_port = 631;
static const char *cfg_device = "/dev/usb/lp0";
static const char *cfg_spool = "/tmp/sp211-spool";
static const char *cfg_name = "RICOH SP 211";
static char cfg_uuid[64];
static const char *cfg_listen = NULL;
static int cfg_verbose = 0;
static time_t start_time;

static void logmsg(const char *fmt, ...) {
  va_list ap;
  char ts[32];
  time_t t = time(NULL);
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
  fprintf(stderr, "%s ", ts);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
#define DBG(...) do { if (cfg_verbose) logmsg(__VA_ARGS__); } while (0)

/* ------------------------------------------------------------------ */
/* media table                                                         */

typedef struct {
  const char *pwg;     /* IPP media name */
  int x, y;            /* hundredths of mm */
  const char *ppd;     /* PPD keyword (lilo2 paper code) */
} media_t;

static const media_t media_list[] = {
  {"iso_a4_210x297mm", 21000, 29700, "A4"},
  {"na_letter_8.5x11in", 21590, 27940, "Letter"},
  {"na_legal_8.5x14in", 21590, 35560, "Legal"},
  {"iso_a5_148x210mm", 14800, 21000, "A5"},
  {"iso_a6_105x148mm", 10500, 14800, "A6"},
  {"jis_b5_182x257mm", 18200, 25700, "B5"},
  {"na_invoice_5.5x8.5in", 13970, 21590, "HalfLetter"},
  {"na_foolscap_8.5x13in", 21590, 33020, "Foolscap"},
};
#define NMEDIA (sizeof media_list / sizeof *media_list)
#define MARGIN 423 /* 12 pt in hundredths of mm, as in the PPD */

/* pixels at 600 dpi for a dimension in hundredths of mm */
static int px600(int hmm) { return (int)((double)hmm / 2540.0 * 600.0 + 0.5); }

static const media_t *media_by_pixels(unsigned w, unsigned h, int dpi) {
  for (unsigned i = 0; i < NMEDIA; i++) {
    int mw = (int)((double)media_list[i].x / 2540.0 * dpi + 0.5);
    int mh = (int)((double)media_list[i].y / 2540.0 * dpi + 0.5);
    if (abs(mw - (int)w) <= 4 * dpi / 300 && abs(mh - (int)h) <= 4 * dpi / 300) return &media_list[i];
  }
  return NULL;
}

typedef struct { const char *ipp; const char *ppd; } map_t;
static const map_t media_types[] = {
  {"stationery", "PlainRecycled"}, {"stationery-recycled", "Recycled"},
  {"stationery-heavyweight", "Thick"}, {"stationery-lightweight", "Thin"},
  {"stationery-letterhead", "Letterhead"}, {"stationery-preprinted", "Preprinted"},
  {"stationery-prepunched", "Prepunched"}, {"stationery-bond", "Bond"},
  {"stationery-colored", "Color"}, {"cardstock", "Cardstock"},
  {"labels", "Label"}, {"envelope", "Envelope"},
};
static const map_t media_sources[] = {{"main", "Tray1"}, {"manual", "Manual"}};

static const char *map_lookup(const map_t *m, size_t n, const char *k) {
  for (size_t i = 0; k && i < n; i++)
    if (!strcmp(m[i].ipp, k)) return m[i].ppd;
  return NULL;
}

/* ------------------------------------------------------------------ */
/* IPP encoding                                                        */

enum {
  TAG_OPERATION = 0x01, TAG_JOB = 0x02, TAG_END = 0x03, TAG_PRINTER = 0x04,
  TAG_UNSUPPORTED_GROUP = 0x05,
  TAG_UNSUPPORTED_VALUE = 0x10, TAG_UNKNOWN = 0x12, TAG_NOVALUE = 0x13,
  TAG_INTEGER = 0x21, TAG_BOOLEAN = 0x22, TAG_ENUM = 0x23,
  TAG_STRING = 0x30, TAG_DATE = 0x31, TAG_RESOLUTION = 0x32, TAG_RANGE = 0x33,
  TAG_BEGIN_COLLECTION = 0x34, TAG_END_COLLECTION = 0x37,
  TAG_TEXT = 0x41, TAG_NAME = 0x42, TAG_KEYWORD = 0x44, TAG_URI = 0x45,
  TAG_URISCHEME = 0x46, TAG_CHARSET = 0x47, TAG_LANGUAGE = 0x48, TAG_MIMETYPE = 0x49,
  TAG_MEMBERNAME = 0x4a,
};

enum {
  OP_PRINT_JOB = 0x0002, OP_VALIDATE_JOB = 0x0004, OP_CREATE_JOB = 0x0005,
  OP_SEND_DOCUMENT = 0x0006, OP_CANCEL_JOB = 0x0008, OP_GET_JOB_ATTRIBUTES = 0x0009,
  OP_GET_JOBS = 0x000a, OP_GET_PRINTER_ATTRIBUTES = 0x000b, OP_CANCEL_MY_JOBS = 0x0039,
  OP_CLOSE_JOB = 0x003b,
  OP_IDENTIFY_PRINTER = 0x003c,
};

enum {
  ST_OK = 0x0000, ST_OK_IGNORED = 0x0001, ST_BAD_REQUEST = 0x0400, ST_NOT_FOUND = 0x0406,
  ST_NOT_POSSIBLE = 0x0404, ST_DOC_FORMAT = 0x040a, ST_ATTR_UNSUPPORTED = 0x040b,
  ST_INTERNAL = 0x0500, ST_OP_UNSUPPORTED = 0x0501, ST_VERSION = 0x0503,
  ST_BAD_CHARSET = 0x040d, ST_JOB_NOT_ACCEPTING = 0x0506,
};

/* parsed attribute (values point into allocated copies) */
typedef struct ipp_val {
  int tag;
  int len;
  unsigned char *data;
  struct ipp_attrs *col; /* for collections */
} ipp_val;

typedef struct ipp_attr {
  int group;
  char *name;
  int nvals;
  ipp_val *vals;
} ipp_attr;

typedef struct ipp_attrs {
  int n, cap;
  ipp_attr *a;
} ipp_attrs;

static void attrs_free(ipp_attrs *as) {
  if (!as) return;
  for (int i = 0; i < as->n; i++) {
    for (int j = 0; j < as->a[i].nvals; j++) {
      free(as->a[i].vals[j].data);
      if (as->a[i].vals[j].col) { attrs_free(as->a[i].vals[j].col); free(as->a[i].vals[j].col); }
    }
    free(as->a[i].vals);
    free(as->a[i].name);
  }
  free(as->a);
  as->a = NULL; as->n = as->cap = 0;
}

static ipp_attr *attrs_add(ipp_attrs *as, int group, const char *name, int nlen) {
  if (as->n == as->cap) {
    as->cap = as->cap ? as->cap * 2 : 16;
    as->a = realloc(as->a, sizeof(ipp_attr) * as->cap);
  }
  ipp_attr *a = &as->a[as->n++];
  a->group = group;
  a->name = strndup(name, nlen);
  a->nvals = 0;
  a->vals = NULL;
  return a;
}

static ipp_val *attr_addval(ipp_attr *a) {
  a->vals = realloc(a->vals, sizeof(ipp_val) * (a->nvals + 1));
  ipp_val *v = &a->vals[a->nvals++];
  memset(v, 0, sizeof *v);
  return v;
}

static ipp_attr *attrs_find(ipp_attrs *as, const char *name) {
  for (int i = 0; as && i < as->n; i++)
    if (!strcmp(as->a[i].name, name)) return &as->a[i];
  return NULL;
}

static const char *attr_str(ipp_attrs *as, const char *name) {
  ipp_attr *a = attrs_find(as, name);
  if (!a || !a->nvals || !a->vals[0].data) return NULL;
  return (const char *)a->vals[0].data; /* data is NUL-terminated */
}

static int attr_int(ipp_attrs *as, const char *name, int dflt) {
  ipp_attr *a = attrs_find(as, name);
  if (!a || !a->nvals || a->vals[0].len != 4) return dflt;
  unsigned char *d = a->vals[0].data;
  return (int)((uint32_t)d[0] << 24 | d[1] << 16 | d[2] << 8 | d[3]);
}

/* ---- response builder ---- */

typedef lilo2_buf obuf;

static void ob_put(obuf *b, const void *p, size_t n) { lilo2_buf_put(b, p, n); }
static void ob_u8(obuf *b, int v) { unsigned char c = (unsigned char)v; ob_put(b, &c, 1); }
static void ob_u16(obuf *b, int v) { ob_u8(b, v >> 8); ob_u8(b, v); }
static void ob_u32(obuf *b, uint32_t v) { ob_u16(b, v >> 16); ob_u16(b, v & 0xffff); }

static void ob_attr_hdr(obuf *b, int tag, const char *name, int vlen) {
  ob_u8(b, tag);
  size_t n = name ? strlen(name) : 0;
  ob_u16(b, (int)n);
  if (n) ob_put(b, name, n);
  ob_u16(b, vlen);
}

static void add_str(obuf *b, int tag, const char *name, const char *v) {
  ob_attr_hdr(b, tag, name, (int)strlen(v));
  ob_put(b, v, strlen(v));
}

static void add_strs(obuf *b, int tag, const char *name, const char *const *v, int n) {
  for (int i = 0; i < n; i++) add_str(b, tag, i ? NULL : name, v[i]);
}

static void add_int(obuf *b, int tag, const char *name, int v) {
  ob_attr_hdr(b, tag, name, 4);
  ob_u32(b, (uint32_t)v);
}

static void add_ints(obuf *b, int tag, const char *name, const int *v, int n) {
  for (int i = 0; i < n; i++) add_int(b, tag, i ? NULL : name, v[i]);
}

static void add_bool(obuf *b, const char *name, int v) {
  ob_attr_hdr(b, TAG_BOOLEAN, name, 1);
  ob_u8(b, v ? 1 : 0);
}

static void add_range(obuf *b, const char *name, int lo, int hi) {
  ob_attr_hdr(b, TAG_RANGE, name, 8);
  ob_u32(b, (uint32_t)lo);
  ob_u32(b, (uint32_t)hi);
}

static void add_res(obuf *b, const char *name, int x, int y) {
  ob_attr_hdr(b, TAG_RESOLUTION, name, 9);
  ob_u32(b, (uint32_t)x);
  ob_u32(b, (uint32_t)y);
  ob_u8(b, 3); /* dots per inch */
}

static void add_date(obuf *b, const char *name, time_t t) {
  struct tm tm;
  gmtime_r(&t, &tm);
  ob_attr_hdr(b, TAG_DATE, name, 11);
  ob_u16(b, tm.tm_year + 1900);
  ob_u8(b, tm.tm_mon + 1); ob_u8(b, tm.tm_mday); ob_u8(b, tm.tm_hour);
  ob_u8(b, tm.tm_min); ob_u8(b, tm.tm_sec); ob_u8(b, 0);
  ob_u8(b, '+'); ob_u8(b, 0); ob_u8(b, 0);
}

static void col_begin(obuf *b, const char *name) { ob_attr_hdr(b, TAG_BEGIN_COLLECTION, name, 0); }
static void col_end(obuf *b) { ob_attr_hdr(b, TAG_END_COLLECTION, NULL, 0); }
static void col_member(obuf *b, const char *member) {
  ob_attr_hdr(b, TAG_MEMBERNAME, NULL, (int)strlen(member));
  ob_put(b, member, strlen(member));
}
static void col_int(obuf *b, const char *member, int v) {
  col_member(b, member);
  ob_attr_hdr(b, TAG_INTEGER, NULL, 4);
  ob_u32(b, (uint32_t)v);
}
static void col_kw(obuf *b, const char *member, const char *v) {
  col_member(b, member);
  ob_attr_hdr(b, TAG_KEYWORD, NULL, (int)strlen(v));
  ob_put(b, v, strlen(v));
}

/* media-col value (as a collection value; name NULL for additional values) */
static void add_media_col(obuf *b, const char *name, const media_t *m, const char *source,
                          int with_type) {
  col_begin(b, name);
  col_member(b, "media-size");
  ob_attr_hdr(b, TAG_BEGIN_COLLECTION, NULL, 0);
  col_int(b, "x-dimension", m->x);
  col_int(b, "y-dimension", m->y);
  col_end(b);
  col_int(b, "media-bottom-margin", MARGIN);
  col_int(b, "media-left-margin", MARGIN);
  col_int(b, "media-right-margin", MARGIN);
  col_int(b, "media-top-margin", MARGIN);
  if (source) col_kw(b, "media-source", source);
  if (with_type) col_kw(b, "media-type", "stationery");
  col_end(b);
}

/* ------------------------------------------------------------------ */
/* HTTP connection with request body reader                            */

typedef struct {
  int fd;
  unsigned char buf[16384];
  size_t pos, end;
  int chunked;
  long long remaining; /* content-length mode, or bytes left in chunk */
  int body_done;
} conn_t;

static ssize_t raw_read(conn_t *c, void *p, size_t n) {
  if (c->pos < c->end) {
    size_t k = c->end - c->pos;
    if (k > n) k = n;
    memcpy(p, c->buf + c->pos, k);
    c->pos += k;
    return (ssize_t)k;
  }
  return read(c->fd, p, n);
}

static int raw_getc(conn_t *c) {
  if (c->pos >= c->end) {
    ssize_t r = read(c->fd, c->buf, sizeof c->buf);
    if (r <= 0) return -1;
    c->pos = 0; c->end = (size_t)r;
  }
  return c->buf[c->pos++];
}

static int read_line(conn_t *c, char *line, size_t max) {
  size_t n = 0;
  int ch;
  while ((ch = raw_getc(c)) >= 0) {
    if (ch == '\n') break;
    if (ch != '\r' && n + 1 < max) line[n++] = (char)ch;
  }
  line[n] = 0;
  return (ch < 0 && n == 0) ? -1 : (int)n;
}

/* reads up to n bytes of the request body; 0 at end */
static ssize_t body_read(conn_t *c, void *p, size_t n) {
  if (c->body_done) return 0;
  if (c->chunked && c->remaining == 0) {
    char line[64];
    if (read_line(c, line, sizeof line) < 0) return -1;
    if (!line[0] && read_line(c, line, sizeof line) < 0) return -1; /* CRLF after chunk */
    c->remaining = strtoll(line, NULL, 16);
    if (c->remaining == 0) {
      while (read_line(c, line, sizeof line) > 0) {} /* trailers */
      c->body_done = 1;
      return 0;
    }
  }
  if (!c->chunked && c->remaining <= 0) { c->body_done = 1; return 0; }
  if ((long long)n > c->remaining) n = (size_t)c->remaining;
  ssize_t r = raw_read(c, p, n);
  if (r <= 0) return -1;
  c->remaining -= r;
  return r;
}

static int body_exact(conn_t *c, void *p, size_t n) {
  unsigned char *q = p;
  while (n) {
    ssize_t r = body_read(c, q, n);
    if (r <= 0) return -1;
    q += r; n -= (size_t)r;
  }
  return 0;
}

static void body_drain(conn_t *c) {
  unsigned char tmp[8192];
  while (body_read(c, tmp, sizeof tmp) > 0) {}
}

static int write_all(int fd, const void *p, size_t n) {
  const unsigned char *q = p;
  while (n) {
    ssize_t w = write(fd, q, n);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) return -1;
    q += w; n -= (size_t)w;
  }
  return 0;
}

/* parses an attribute list from the body; stops after end-of-attributes */
static int parse_attrs(conn_t *c, ipp_attrs *as, int *group_io, int in_col) {
  ipp_attr *cur = NULL;
  int group = group_io ? *group_io : 0;
  for (;;) {
    unsigned char tag;
    if (body_exact(c, &tag, 1)) return -1;
    if (!in_col && tag < 0x10) {
      if (tag == TAG_END) return 0;
      group = tag; cur = NULL;
      if (group_io) *group_io = group;
      continue;
    }
    unsigned char l2[2];
    if (body_exact(c, l2, 2)) return -1;
    int nlen = l2[0] << 8 | l2[1];
    char name[256];
    if (nlen >= (int)sizeof name || body_exact(c, name, nlen)) return -1;
    if (body_exact(c, l2, 2)) return -1;
    int vlen = l2[0] << 8 | l2[1];
    unsigned char *v = malloc(vlen + 1);
    if (!v || (vlen && body_exact(c, v, vlen))) { free(v); return -1; }
    v[vlen] = 0;

    if (in_col) {
      if (tag == TAG_END_COLLECTION) { free(v); return 0; }
      if (tag == TAG_MEMBERNAME) {
        cur = attrs_add(as, group, (char *)v, vlen);
        free(v);
        continue;
      }
    }
    if (nlen) cur = attrs_add(as, group, name, nlen);
    if (!cur) { free(v); return -1; }
    ipp_val *val = attr_addval(cur);
    val->tag = tag; val->len = vlen; val->data = v;
    if (tag == TAG_BEGIN_COLLECTION) {
      val->col = calloc(1, sizeof(ipp_attrs));
      if (parse_attrs(c, val->col, NULL, 1)) return -1;
    }
  }
}

/* ------------------------------------------------------------------ */
/* jobs                                                                */

enum { JS_PENDING = 3, JS_HELD = 4, JS_PROCESSING = 5, JS_STOPPED = 6,
       JS_CANCELED = 7, JS_ABORTED = 8, JS_COMPLETED = 9 };

typedef struct job {
  int id;
  int state;
  char name[256], user[256], format[64], reason[64];
  char spool[512];
  int have_doc, doc_spooled, closed, cancel;
  int copies;
  int duplex, tumble;
  int paper_override; /* lilo2 paper code from media/media-col, -1 = from raster */
  int media_code, tray_code;
  int impressions;
  time_t created, processing, completed;
  struct job *next;
} job_t;

static pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t jobs_cond = PTHREAD_COND_INITIALIZER;
static job_t *jobs = NULL;
static int next_job_id = 1;
static char printer_reason[64] = "none";

static job_t *job_find(int id) {
  for (job_t *j = jobs; j; j = j->next)
    if (j->id == id) return j;
  return NULL;
}

static int printer_state(void) {
  for (job_t *j = jobs; j; j = j->next)
    if (j->state == JS_PROCESSING) return 4;
  return 3;
}

static int queued_count(void) {
  int n = 0;
  for (job_t *j = jobs; j; j = j->next)
    if (j->state == JS_PENDING || j->state == JS_PROCESSING || j->state == JS_HELD) n++;
  return n;
}

/* keep at most 50 finished jobs */
static void prune_jobs(void) {
  int n = 0;
  for (job_t **pp = &jobs; *pp;) {
    job_t *j = *pp;
    if (j->state >= JS_CANCELED && ++n > 50) {
      *pp = j->next;
      free(j);
    } else pp = &j->next;
  }
}

/* ------------------------------------------------------------------ */
/* raster decoding (Apple raster / PWG raster) -> lilo2                */

typedef struct {
  FILE *f;
  int is_urf;
  unsigned width, height, bpp, dpi_x, dpi_y;
  int cspace;        /* 0 gray-like (255 white), 1 black (255 black), 2 rgb */
  unsigned num_copies;
  unsigned page_w_pt, page_h_pt; /* from PWG header (0 for URF) */
  int duplex, tumble;
  unsigned pages_left; /* URF only */
} rast_t;

static uint32_t be32(const unsigned char *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int rast_open(rast_t *r, FILE *f) {
  unsigned char sync[8];
  memset(r, 0, sizeof *r);
  r->f = f;
  if (fread(sync, 1, 4, f) != 4) return -1;
  if (!memcmp(sync, "UNIR", 4)) {
    if (fread(sync + 4, 1, 4, f) != 4 || memcmp(sync, "UNIRAST", 8)) return -1;
    unsigned char n[4];
    if (fread(n, 1, 4, f) != 4) return -1;
    r->is_urf = 1;
    r->pages_left = be32(n);
    if (!r->pages_left) r->pages_left = 0xffffffff;
    return 0;
  }
  if (!memcmp(sync, "RaS2", 4)) return 0;
  return -1;
}

/* reads the next page header; 1 = page, 0 = end, -1 = error */
static int rast_next_page(rast_t *r) {
  if (r->is_urf) {
    unsigned char h[32];
    if (!r->pages_left) return 0;
    size_t n = fread(h, 1, 32, r->f);
    if (n == 0) return 0;
    if (n != 32) return -1;
    r->pages_left--;
    r->bpp = h[0];
    int cs = h[1];
    r->cspace = (cs == 1 || cs == 3 || cs == 5) ? 2 : (cs == 6 ? -1 : 0);
    r->duplex = h[2] > 1;
    r->tumble = h[2] == 3;
    r->width = be32(h + 12);
    r->height = be32(h + 16);
    r->dpi_x = r->dpi_y = be32(h + 20);
    r->num_copies = 0;
    r->page_w_pt = r->page_h_pt = 0;
  } else {
    unsigned char h[1796];
    size_t n = fread(h, 1, sizeof h, r->f);
    if (n == 0) return 0;
    if (n != sizeof h) return -1;
    r->dpi_x = be32(h + 276);
    r->dpi_y = be32(h + 280);
    r->duplex = be32(h + 272) != 0;
    r->tumble = be32(h + 368) != 0;
    r->num_copies = be32(h + 340);
    r->page_w_pt = be32(h + 352);
    r->page_h_pt = be32(h + 356);
    r->width = be32(h + 372);
    r->height = be32(h + 376);
    r->bpp = be32(h + 388);
    unsigned cs = be32(h + 400);
    r->cspace = (cs == 3) ? 1 : (cs == 1 || cs == 19 || cs == 20) ? 2 : (cs == 18 || cs == 0) ? 0 : -1;
  }
  if (r->cspace < 0 || !(r->bpp == 1 || r->bpp == 8 || r->bpp == 24) ||
      (r->bpp == 24 && r->cspace != 2) || (r->bpp != 24 && r->cspace == 2) ||
      (r->bpp == 1 && r->cspace != 1) ||
      !r->width || !r->height || r->width > 20000 || r->height > 30000 || !r->dpi_x || !r->dpi_y)
    return -1;
  return 1;
}

/* decodes one compressed line group into line (bytes), returns repeat count
 * (number of times the line occurs, >= 1) or -1 */
static int rast_read_line(rast_t *r, unsigned char *line) {
  unsigned bpp_bytes = r->bpp == 1 ? 1 : r->bpp / 8;
  size_t bpl = r->bpp == 1 ? (r->width + 7) / 8 : (size_t)r->width * bpp_bytes;
  unsigned pix_bytes = r->bpp == 1 ? 1 : bpp_bytes;
  int rep = fgetc(r->f);
  if (rep < 0) return -1;
  size_t pos = 0;
  unsigned char white = (r->cspace == 1) ? 0x00 : 0xff;
  while (pos < bpl) {
    int c = fgetc(r->f);
    if (c < 0) return -1;
    if (c == 128) { memset(line + pos, white, bpl - pos); pos = bpl; break; }
    if (c < 128) {
      unsigned char px[4];
      if (fread(px, 1, pix_bytes, r->f) != pix_bytes) return -1;
      for (int k = 0; k <= c && pos < bpl; k++) {
        size_t m = pix_bytes;
        if (pos + m > bpl) m = bpl - pos;
        memcpy(line + pos, px, m);
        pos += m;
      }
    } else {
      size_t m = (size_t)(257 - c) * pix_bytes;
      if (pos + m > bpl) return -1;
      if (fread(line + pos, 1, m, r->f) != m) return -1;
      pos += m;
    }
  }
  return rep + 1;
}

/* converts a decoded raster line to 8-bit ink (255 = black), resampled to
 * 600 dpi horizontally, starting at pixel x0 (in 600 dpi units) */
static void line_to_ink(const rast_t *r, const unsigned char *line, unsigned char *ink,
                        unsigned x0, unsigned outw) {
  for (unsigned x = 0; x < outw; x++) {
    unsigned sx = (unsigned)(((unsigned long long)(x + x0) * r->dpi_x) / 600);
    unsigned v;
    if (sx >= r->width) { ink[x] = 0; continue; }
    if (r->bpp == 1) v = (line[sx >> 3] >> (7 - (sx & 7)) & 1) ? 255 : 0;
    else if (r->bpp == 8) v = r->cspace == 1 ? line[sx] : 255 - line[sx];
    else {
      const unsigned char *p = line + sx * 3;
      v = 255 - (p[0] * 299 + p[1] * 587 + p[2] * 114 + 500) / 1000;
    }
    ink[x] = (unsigned char)v;
  }
}

/* Renders the current raster page to lilo2 GDIP/GDIB blocks. */
static int render_page(rast_t *r, job_t *j, lilo2_buf *out, int flags, int pageno,
                       int tray, int rotate, int *paper_out) {
  /* full page size in 600 dpi pixels */
  unsigned fw = (unsigned)((unsigned long long)r->width * 600 / r->dpi_x);
  unsigned fh = (unsigned)((unsigned long long)r->height * 600 / r->dpi_y);
  const media_t *m = media_by_pixels(r->width, r->height, (int)r->dpi_x);
  int paper = j->paper_override >= 0 ? j->paper_override : (m ? lilo2_paper_code(m->ppd) : 0);
  if (paper_out) *paper_out = paper;

  /* imageable area as in the PPD: 12 pt (100 px) margins on every side */
  unsigned pw = m ? (unsigned)px600(m->x) : fw, ph = m ? (unsigned)px600(m->y) : fh;
  unsigned w = pw > 200 ? pw - 200 : pw, h = ph > 200 ? ph - 200 : ph;
  unsigned x0 = 100, y0 = 100;

  lilo2_page pg = {0};
  pg.width = w; pg.height = h;
  pg.page_w_pt = m ? (unsigned)((long long)m->x * 72 / 2540) : fw * 72 / 600;
  pg.page_h_pt = m ? (unsigned)((long long)m->y * 72 / 2540) : fh * 72 / 600;
  pg.paper = paper; pg.tray = tray; pg.media = j->media_code;
  pg.flags = flags; pg.pageno = pageno;
  if (lilo2_page_begin(&pg, out)) return -1;

  size_t bpl = r->bpp == 1 ? (r->width + 7) / 8 : (size_t)r->width * (r->bpp / 8);
  unsigned char *line = malloc(bpl);
  unsigned char *ink = malloc(w);
  unsigned char *page = rotate ? calloc((size_t)w, h) : NULL;
  unsigned char *blank = calloc(w, 1);
  if (!line || !ink || !blank || (rotate && !page)) { free(line); free(ink); free(page); free(blank); return -1; }

  /* source line y covers output lines [y*600/dpi_y, (y+1)*600/dpi_y) */
  unsigned out_y = 0, src_y = 0;
  int err = 0;
  while (src_y < r->height) {
    int rep = rast_read_line(r, line);
    if (rep < 0) { err = 1; break; }
    line_to_ink(r, line, ink, x0, w);
    for (int k = 0; k < rep && src_y < r->height; k++, src_y++) {
      unsigned yy_end = (unsigned)(((unsigned long long)(src_y + 1) * 600) / r->dpi_y);
      for (unsigned yy = (unsigned)(((unsigned long long)src_y * 600) / r->dpi_y); yy < yy_end; yy++) {
        if (yy < y0 || yy - y0 >= h) continue;
        unsigned oy = yy - y0;
        if (rotate) {
          unsigned char *dst = page + (size_t)(h - 1 - oy) * w;
          for (unsigned x = 0; x < w; x++) dst[w - 1 - x] = ink[x];
        } else {
          while (out_y < oy) { lilo2_page_line(&pg, blank); out_y++; } /* gaps */
          lilo2_page_line(&pg, ink);
          out_y++;
        }
      }
    }
  }
  if (rotate) for (unsigned y = 0; y < h; y++) lilo2_page_line(&pg, page + (size_t)y * w);
  lilo2_page_end(&pg);
  free(line); free(ink); free(page); free(blank);
  return err ? -1 : 0;
}

/* opens the printer device, waiting for it to appear (printer switched off,
 * cable unplugged, ...). Returns fd or -1 if the job was canceled. */
static int open_device(job_t *j) {
  int warned = 0;
  for (;;) {
    int fd;
    if (!strcmp(cfg_device, "-")) fd = dup(1);
    else if (!strncmp(cfg_device, "/dev/", 5)) fd = open(cfg_device, O_WRONLY);
    else fd = open(cfg_device, O_WRONLY | O_CREAT | O_APPEND, 0644); /* testing: file */
    if (fd >= 0) {
      pthread_mutex_lock(&jobs_lock);
      strcpy(printer_reason, "none");
      pthread_mutex_unlock(&jobs_lock);
      return fd;
    }
    if (!warned) { logmsg("waiting for printer device %s: %s", cfg_device, strerror(errno)); warned = 1; }
    pthread_mutex_lock(&jobs_lock);
    strcpy(printer_reason, "offline-report");
    int cancel = j->cancel;
    pthread_mutex_unlock(&jobs_lock);
    if (cancel) return -1;
    sleep(2);
  }
}

static void process_job(job_t *j) {
  FILE *f = fopen(j->spool, "rb");
  rast_t r;
  int ok = 0, pages = 0;
  lilo2_buf out = {0};
  lilo2_buf *dpages = NULL; /* manual duplex */
  int dev = -1;

  if (!f || rast_open(&r, f)) { logmsg("job %d: unsupported or empty document", j->id); goto done; }

  lilo2_job hdr = {0};
  hdr.copies = j->copies;
  hdr.title = j->name;
  hdr.user = j->user;
  {
    time_t t = j->created;
    struct tm lt;
    localtime_r(&t, &lt);
    hdr.ctime = (uint32_t)(t + lt.tm_gmtoff);
  }

  int st, first = 1, duplex = 0, tumble = 0;
  while ((st = rast_next_page(&r)) == 1) {
    pthread_mutex_lock(&jobs_lock);
    int cancel = j->cancel;
    pthread_mutex_unlock(&jobs_lock);
    if (cancel) break;
    pages++;
    if (first) {
      int paper_probe;
      const media_t *m = media_by_pixels(r.width, r.height, (int)r.dpi_x);
      paper_probe = j->paper_override >= 0 ? j->paper_override : (m ? lilo2_paper_code(m->ppd) : 0);
      duplex = (j->duplex || r.duplex) && lilo2_duplex_allowed(paper_probe);
      tumble = j->duplex ? j->tumble : r.tumble;
      hdr.duplex = duplex;
      if (r.num_copies > 1 && j->copies <= 1) hdr.copies = (int)r.num_copies;
      lilo2_job_header(&out, &hdr);
      if ((dev = open_device(j)) < 0) goto done;
      if (write_all(dev, out.d, out.len)) { logmsg("job %d: write error", j->id); goto done; }
      out.len = 0;
      first = 0;
    }
    if (!duplex) {
      out.len = 0;
      if (render_page(&r, j, &out, LILO2_PAGE_NORMAL, pages, j->tray_code, 0, NULL)) {
        logmsg("job %d: bad raster data on page %d", j->id, pages); goto done;
      }
      if (write_all(dev, out.d, out.len)) { logmsg("job %d: write error", j->id); goto done; }
    } else {
      dpages = realloc(dpages, sizeof(lilo2_buf) * pages);
      lilo2_buf *pb = &dpages[pages - 1];
      memset(pb, 0, sizeof *pb);
      int even = pages % 2 == 0;
      if (render_page(&r, j, pb, even ? LILO2_PAGE_DUPLEX_FRONT : LILO2_PAGE_DUPLEX_BACK, pages,
                      even ? j->tray_code : 2, even && !tumble, NULL)) {
        logmsg("job %d: bad raster data on page %d", j->id, pages); goto done;
      }
    }
    pthread_mutex_lock(&jobs_lock);
    j->impressions = pages;
    pthread_mutex_unlock(&jobs_lock);
  }
  if (st < 0) { logmsg("job %d: bad raster data", j->id); goto done; }
  if (first) { logmsg("job %d: no pages", j->id); ok = 1; goto done; }
  if (duplex) {
    for (int i = 2; i <= pages; i += 2)
      if (write_all(dev, dpages[i - 1].d, dpages[i - 1].len)) goto done;
    for (int i = 1; i <= pages; i += 2) {
      if (i == 1) dpages[0].d[33] = LILO2_PAGE_DUPLEX_BACK1;
      if (write_all(dev, dpages[i - 1].d, dpages[i - 1].len)) goto done;
    }
  }
  out.len = 0;
  lilo2_job_end(&out);
  if (write_all(dev, out.d, out.len)) goto done;
  ok = 1;
  logmsg("job %d: printed %d page(s)%s", j->id, pages, duplex ? " (manual duplex)" : "");

done:
  if (dev >= 0) close(dev);
  if (f) fclose(f);
  if (dpages) { for (int i = 0; i < pages; i++) lilo2_buf_free(&dpages[i]); free(dpages); }
  lilo2_buf_free(&out);
  unlink(j->spool);
  pthread_mutex_lock(&jobs_lock);
  j->completed = time(NULL);
  j->state = j->cancel ? JS_CANCELED : ok ? JS_COMPLETED : JS_ABORTED;
  strcpy(j->reason, j->cancel ? "job-canceled-by-user" : ok ? "job-completed-successfully"
                                                           : "aborted-by-system");
  prune_jobs();
  pthread_mutex_unlock(&jobs_lock);
}

static void *worker(void *arg) {
  (void)arg;
  for (;;) {
    pthread_mutex_lock(&jobs_lock);
    job_t *j = NULL;
    for (;;) {
      /* oldest pending job with its document */
      for (job_t *k = jobs; k; k = k->next)
        if (k->state == JS_PENDING && k->have_doc && (!j || k->id < j->id)) j = k;
      if (j) break;
      pthread_cond_wait(&jobs_cond, &jobs_lock);
    }
    j->state = JS_PROCESSING;
    strcpy(j->reason, "job-printing");
    j->processing = time(NULL);
    pthread_mutex_unlock(&jobs_lock);
    process_job(j);
  }
  return NULL;
}

/* ------------------------------------------------------------------ */
/* IPP request handling                                                */

typedef struct {
  conn_t *c;
  char host[256];      /* Host header (for printer-uri) */
  int version, op, reqid;
  ipp_attrs attrs;
  obuf resp;
  int status;
  const char *status_msg;
  ipp_attrs unsupported_names; /* names only */
} req_t;

static void printer_uri(req_t *q, char *buf, size_t n) {
  snprintf(buf, n, "ipp://%s/ipp/print", q->host[0] ? q->host : "localhost");
}

static void resp_begin(req_t *q, int status) {
  q->status = status;
  q->resp.len = 0;
  ob_u8(&q->resp, q->version >> 8 ? q->version >> 8 : 1);
  ob_u8(&q->resp, q->version >> 8 ? q->version & 0xff : 1);
  ob_u16(&q->resp, status);
  ob_u32(&q->resp, (uint32_t)q->reqid);
  ob_u8(&q->resp, TAG_OPERATION);
  add_str(&q->resp, TAG_CHARSET, "attributes-charset", "utf-8");
  add_str(&q->resp, TAG_LANGUAGE, "attributes-natural-language", "en");
  if (q->status_msg) add_str(&q->resp, TAG_TEXT, "status-message", q->status_msg);
}

/* requested-attributes filter */
static int wants(req_t *q, const char *name, const char *group) {
  ipp_attr *ra = attrs_find(&q->attrs, "requested-attributes");
  if (!ra) return 1;
  for (int i = 0; i < ra->nvals; i++) {
    const char *v = (const char *)ra->vals[i].data;
    if (!strcmp(v, "all") || !strcmp(v, name) || (group && !strcmp(v, group))) return 1;
  }
  return 0;
}

static void printer_attrs(req_t *q) {
  obuf *b = &q->resp;
  char uri[512], more[512], devid[256], uuid[128];
  printer_uri(q, uri, sizeof uri);
  snprintf(more, sizeof more, "http://%s/", q->host[0] ? q->host : "localhost");
  snprintf(devid, sizeof devid, "MFG:RICOH;MDL:SP 211;CMD:URF,PWGRaster;");
  snprintf(uuid, sizeof uuid, "urn:uuid:%s", cfg_uuid);
  ob_u8(b, TAG_PRINTER);

#define W(n) if (wants(q, n, "printer-description"))
  static const char *const charsets[] = {"utf-8", "us-ascii"};
  W("charset-configured") add_str(b, TAG_CHARSET, "charset-configured", "utf-8");
  W("charset-supported") add_strs(b, TAG_CHARSET, "charset-supported", charsets, 2);
  W("color-supported") add_bool(b, "color-supported", 0);
  W("compression-supported") add_str(b, TAG_KEYWORD, "compression-supported", "none");
  W("copies-default") add_int(b, TAG_INTEGER, "copies-default", 1);
  W("copies-supported") add_range(b, "copies-supported", 1, 99);
  static const char *const formats[] = {"image/pwg-raster", "image/urf"};
  W("document-format-default") add_str(b, TAG_MIMETYPE, "document-format-default", "image/urf");
  W("document-format-supported") add_strs(b, TAG_MIMETYPE, "document-format-supported", formats, 2);
  W("finishings-default") add_int(b, TAG_ENUM, "finishings-default", 3);
  W("finishings-supported") add_int(b, TAG_ENUM, "finishings-supported", 3);
  W("generated-natural-language-supported") add_str(b, TAG_LANGUAGE, "generated-natural-language-supported", "en");
  W("identify-actions-default") add_str(b, TAG_KEYWORD, "identify-actions-default", "display");
  W("identify-actions-supported") add_str(b, TAG_KEYWORD, "identify-actions-supported", "display");
  static const char *const features[] = {"airprint-1.4", "ipp-everywhere"};
  W("ipp-features-supported") add_strs(b, TAG_KEYWORD, "ipp-features-supported", features, 2);
  static const char *const versions[] = {"1.1", "2.0"};
  W("ipp-versions-supported") add_strs(b, TAG_KEYWORD, "ipp-versions-supported", versions, 2);
  static const char *const jca[] = {"copies", "media", "media-col", "sides", "print-quality",
                                    "print-color-mode", "printer-resolution", "orientation-requested",
                                    "output-bin"};
  W("job-creation-attributes-supported")
    add_strs(b, TAG_KEYWORD, "job-creation-attributes-supported", jca, 9);
  W("job-ids-supported") add_bool(b, "job-ids-supported", 1);
  W("landscape-orientation-requested-preferred") add_int(b, TAG_ENUM, "landscape-orientation-requested-preferred", 5);
  static const int margins[] = {MARGIN};
  W("media-bottom-margin-supported") add_ints(b, TAG_INTEGER, "media-bottom-margin-supported", margins, 1);
  W("media-left-margin-supported") add_ints(b, TAG_INTEGER, "media-left-margin-supported", margins, 1);
  W("media-right-margin-supported") add_ints(b, TAG_INTEGER, "media-right-margin-supported", margins, 1);
  W("media-top-margin-supported") add_ints(b, TAG_INTEGER, "media-top-margin-supported", margins, 1);
  W("media-col-database") {
    for (unsigned i = 0; i < NMEDIA; i++)
      add_media_col(b, i ? NULL : "media-col-database", &media_list[i], NULL, 0);
  }
  W("media-col-default") add_media_col(b, "media-col-default", &media_list[0], "main", 1);
  W("media-col-ready") add_media_col(b, "media-col-ready", &media_list[0], "main", 1);
  static const char *const colsup[] = {"media-bottom-margin", "media-left-margin", "media-right-margin",
                                       "media-size", "media-source", "media-top-margin", "media-type"};
  W("media-col-supported") add_strs(b, TAG_KEYWORD, "media-col-supported", colsup, 7);
  W("media-default") add_str(b, TAG_KEYWORD, "media-default", media_list[0].pwg);
  W("media-ready") add_str(b, TAG_KEYWORD, "media-ready", media_list[0].pwg);
  W("media-supported") {
    for (unsigned i = 0; i < NMEDIA; i++) add_str(b, TAG_KEYWORD, i ? NULL : "media-supported", media_list[i].pwg);
  }
  W("media-size-supported") {
    for (unsigned i = 0; i < NMEDIA; i++) {
      col_begin(b, i ? NULL : "media-size-supported");
      col_int(b, "x-dimension", media_list[i].x);
      col_int(b, "y-dimension", media_list[i].y);
      col_end(b);
    }
  }
  static const char *const sources[] = {"main", "manual"};
  W("media-source-supported") add_strs(b, TAG_KEYWORD, "media-source-supported", sources, 2);
  W("media-type-supported") {
    for (unsigned i = 0; i < sizeof media_types / sizeof *media_types; i++)
      add_str(b, TAG_KEYWORD, i ? NULL : "media-type-supported", media_types[i].ipp);
  }
  W("multiple-document-jobs-supported") add_bool(b, "multiple-document-jobs-supported", 0);
  W("multiple-operation-time-out-action") add_str(b, TAG_KEYWORD, "multiple-operation-time-out-action", "abort-job");
  W("overrides-supported") {
    static const char *const ov[] = {"document-number", "pages"};
    add_strs(b, TAG_KEYWORD, "overrides-supported", ov, 2);
  }
  W("preferred-attributes-supported") add_bool(b, "preferred-attributes-supported", 0);
  W("print-content-optimize-default") add_str(b, TAG_KEYWORD, "print-content-optimize-default", "auto");
  W("print-content-optimize-supported") add_str(b, TAG_KEYWORD, "print-content-optimize-supported", "auto");
  W("print-rendering-intent-default") add_str(b, TAG_KEYWORD, "print-rendering-intent-default", "auto");
  W("print-rendering-intent-supported") add_str(b, TAG_KEYWORD, "print-rendering-intent-supported", "auto");
  W("printer-config-change-date-time") add_date(b, "printer-config-change-date-time", start_time);
  W("printer-config-change-time") add_int(b, TAG_INTEGER, "printer-config-change-time", 1);
  W("printer-current-time") add_date(b, "printer-current-time", time(NULL));
  W("printer-geo-location") ob_attr_hdr(b, TAG_UNKNOWN, "printer-geo-location", 0);
  W("printer-get-attributes-supported") add_str(b, TAG_KEYWORD, "printer-get-attributes-supported", "document-format");
  W("printer-icons") {
    char icon[600];
    snprintf(icon, sizeof icon, "http://%s/icon.png", q->host[0] ? q->host : "localhost");
    add_str(b, TAG_URI, "printer-icons", icon);
  }
  W("printer-organization") add_str(b, TAG_TEXT, "printer-organization", "");
  W("printer-organizational-unit") add_str(b, TAG_TEXT, "printer-organizational-unit", "");
  W("printer-state-change-date-time") add_date(b, "printer-state-change-date-time", start_time);
  W("printer-state-change-time") add_int(b, TAG_INTEGER, "printer-state-change-time", 1);
  W("printer-supply") {
    static const char supply[] = "index=1;class=supplyThatIsConsumed;type=toner;unit=percent;"
                                 "maxcapacity=100;level=-2;colorantname=black;";
    ob_attr_hdr(b, TAG_STRING, "printer-supply", (int)strlen(supply));
    ob_put(b, supply, strlen(supply));
  }
  W("printer-supply-description") add_str(b, TAG_TEXT, "printer-supply-description", "Black Toner");
  W("printer-supply-info-uri") add_str(b, TAG_URI, "printer-supply-info-uri", more);
  W("multiple-operation-time-out") add_int(b, TAG_INTEGER, "multiple-operation-time-out", 60);
  W("natural-language-configured") add_str(b, TAG_LANGUAGE, "natural-language-configured", "en");
  static const int ops[] = {OP_PRINT_JOB, OP_VALIDATE_JOB, OP_CREATE_JOB, OP_SEND_DOCUMENT,
                            OP_CANCEL_JOB, OP_GET_JOB_ATTRIBUTES, OP_GET_JOBS,
                            OP_GET_PRINTER_ATTRIBUTES, OP_CANCEL_MY_JOBS, OP_CLOSE_JOB,
                            OP_IDENTIFY_PRINTER};
  W("operations-supported") add_ints(b, TAG_ENUM, "operations-supported", ops, 11);
  W("orientation-requested-default") add_int(b, TAG_ENUM, "orientation-requested-default", 3);
  static const int orient[] = {3, 4, 5, 6};
  W("orientation-requested-supported") add_ints(b, TAG_ENUM, "orientation-requested-supported", orient, 4);
  W("output-bin-default") add_str(b, TAG_KEYWORD, "output-bin-default", "face-down");
  W("output-bin-supported") add_str(b, TAG_KEYWORD, "output-bin-supported", "face-down");
  W("pages-per-minute") add_int(b, TAG_INTEGER, "pages-per-minute", 23);
  W("pdl-override-supported") add_str(b, TAG_KEYWORD, "pdl-override-supported", "attempted");
  W("print-color-mode-default") add_str(b, TAG_KEYWORD, "print-color-mode-default", "monochrome");
  W("print-color-mode-supported") add_str(b, TAG_KEYWORD, "print-color-mode-supported", "monochrome");
  W("print-quality-default") add_int(b, TAG_ENUM, "print-quality-default", 4);
  static const int quality[] = {3, 4, 5};
  W("print-quality-supported") add_ints(b, TAG_ENUM, "print-quality-supported", quality, 3);
  W("printer-device-id") add_str(b, TAG_TEXT, "printer-device-id", devid);
  W("printer-dns-sd-name") add_str(b, TAG_NAME, "printer-dns-sd-name", cfg_name);
  W("printer-info") add_str(b, TAG_TEXT, "printer-info", cfg_name);
  W("printer-is-accepting-jobs") add_bool(b, "printer-is-accepting-jobs", 1);
  W("printer-kind") add_str(b, TAG_KEYWORD, "printer-kind", "document");
  W("printer-location") add_str(b, TAG_TEXT, "printer-location", "");
  W("printer-make-and-model") add_str(b, TAG_TEXT, "printer-make-and-model", "RICOH SP 211");
  W("printer-more-info") add_str(b, TAG_URI, "printer-more-info", more);
  W("printer-name") add_str(b, TAG_NAME, "printer-name", cfg_name);
  W("printer-resolution-default") add_res(b, "printer-resolution-default", 600, 600);
  W("printer-resolution-supported") add_res(b, "printer-resolution-supported", 600, 600);
  pthread_mutex_lock(&jobs_lock);
  int pstate = printer_state(), queued = queued_count();
  char reason[64];
  strcpy(reason, printer_reason);
  pthread_mutex_unlock(&jobs_lock);
  W("printer-state") add_int(b, TAG_ENUM, "printer-state", pstate);
  W("printer-state-reasons") add_str(b, TAG_KEYWORD, "printer-state-reasons", reason);
  W("printer-state-message") add_str(b, TAG_TEXT, "printer-state-message",
                                     strcmp(reason, "none") ? "Printer not connected" : "");
  W("printer-up-time") add_int(b, TAG_INTEGER, "printer-up-time", (int)(time(NULL) - start_time + 1));
  W("printer-uri-supported") add_str(b, TAG_URI, "printer-uri-supported", uri);
  W("printer-uuid") add_str(b, TAG_URI, "printer-uuid", uuid);
  W("pwg-raster-document-resolution-supported") add_res(b, "pwg-raster-document-resolution-supported", 600, 600);
  W("pwg-raster-document-sheet-back") add_str(b, TAG_KEYWORD, "pwg-raster-document-sheet-back", "normal");
  static const char *const pwgtypes[] = {"black_1", "sgray_8"};
  W("pwg-raster-document-type-supported") add_strs(b, TAG_KEYWORD, "pwg-raster-document-type-supported", pwgtypes, 2);
  W("queued-job-count") add_int(b, TAG_INTEGER, "queued-job-count", queued);
  static const char *const sides[] = {"one-sided", "two-sided-long-edge", "two-sided-short-edge"};
  W("sides-default") add_str(b, TAG_KEYWORD, "sides-default", "one-sided");
  W("sides-supported") add_strs(b, TAG_KEYWORD, "sides-supported", sides, 3);
  W("uri-authentication-supported") add_str(b, TAG_KEYWORD, "uri-authentication-supported", "none");
  W("uri-security-supported") add_str(b, TAG_KEYWORD, "uri-security-supported", "none");
  static const char *const urf[] = {"V1.4", "W8", "RS600", "CP1", "DM1", "IS1-20",
                                    "MT1-2-3-4-5-6-8-9-10-11-12-13"};
  W("urf-supported") add_strs(b, TAG_KEYWORD, "urf-supported", urf, 7);
  W("which-jobs-supported") {
    static const char *const wj[] = {"completed", "not-completed", "all"};
    add_strs(b, TAG_KEYWORD, "which-jobs-supported", wj, 3);
  }
#undef W
}

static void job_attrs(req_t *q, job_t *j, int full) {
  obuf *b = &q->resp;
  char uri[512], juri[600];
  printer_uri(q, uri, sizeof uri);
  snprintf(juri, sizeof juri, "%s/%d", uri, j->id);
  ob_u8(b, TAG_JOB);
  if (!full || wants(q, "job-id", "job-description")) add_int(b, TAG_INTEGER, "job-id", j->id);
  if (!full || wants(q, "job-uri", "job-description")) add_str(b, TAG_URI, "job-uri", juri);
  if (!full || wants(q, "job-state", "job-description")) add_int(b, TAG_ENUM, "job-state", j->state);
  if (!full || wants(q, "job-state-reasons", "job-description"))
    add_str(b, TAG_KEYWORD, "job-state-reasons", j->reason);
  if (!full) return;
  if (wants(q, "job-name", "job-description")) add_str(b, TAG_NAME, "job-name", j->name);
  if (wants(q, "job-originating-user-name", "job-description"))
    add_str(b, TAG_NAME, "job-originating-user-name", j->user);
  if (wants(q, "job-printer-uri", "job-description")) add_str(b, TAG_URI, "job-printer-uri", uri);
  if (wants(q, "job-printer-up-time", "job-description"))
    add_int(b, TAG_INTEGER, "job-printer-up-time", (int)(time(NULL) - start_time + 1));
  if (wants(q, "job-impressions-completed", "job-description"))
    add_int(b, TAG_INTEGER, "job-impressions-completed", j->impressions);
  if (wants(q, "time-at-creation", "job-description"))
    add_int(b, TAG_INTEGER, "time-at-creation", (int)(j->created - start_time + 1));
  if (wants(q, "time-at-processing", "job-description")) {
    if (j->processing) add_int(b, TAG_INTEGER, "time-at-processing", (int)(j->processing - start_time + 1));
    else ob_attr_hdr(b, TAG_NOVALUE, "time-at-processing", 0);
  }
  if (wants(q, "time-at-completed", "job-description")) {
    if (j->completed) add_int(b, TAG_INTEGER, "time-at-completed", (int)(j->completed - start_time + 1));
    else ob_attr_hdr(b, TAG_NOVALUE, "time-at-completed", 0);
  }
}

/* applies job template attributes; returns 0 or an IPP status */
static int apply_job_template(req_t *q, job_t *j) {
  ipp_attrs *as = &q->attrs;
  j->copies = attr_int(as, "copies", 1);
  if (j->copies < 1 || j->copies > 99) return ST_ATTR_UNSUPPORTED;
  j->paper_override = -1;
  j->media_code = 0;
  j->tray_code = 2; /* Tray 1 */
  const char *media = attr_str(as, "media");
  if (media) {
    for (unsigned i = 0; i < NMEDIA; i++)
      if (!strcmp(media, media_list[i].pwg)) j->paper_override = lilo2_paper_code(media_list[i].ppd);
  }
  ipp_attr *mc = attrs_find(as, "media-col");
  if (mc && mc->nvals && mc->vals[0].col) {
    ipp_attrs *col = mc->vals[0].col;
    ipp_attr *ms = attrs_find(col, "media-size");
    if (ms && ms->nvals && ms->vals[0].col) {
      int x = attr_int(ms->vals[0].col, "x-dimension", 0), y = attr_int(ms->vals[0].col, "y-dimension", 0);
      for (unsigned i = 0; i < NMEDIA; i++)
        if (abs(media_list[i].x - x) < 100 && abs(media_list[i].y - y) < 100)
          j->paper_override = lilo2_paper_code(media_list[i].ppd);
    }
    const char *type = map_lookup(media_types, sizeof media_types / sizeof *media_types,
                                  attr_str(col, "media-type"));
    if (type) j->media_code = lilo2_media_code(type);
    const char *src = map_lookup(media_sources, 2, attr_str(col, "media-source"));
    if (src) j->tray_code = lilo2_tray_code(src);
  }
  const char *sides = attr_str(as, "sides");
  j->duplex = sides && strncmp(sides, "two-sided", 9) == 0;
  j->tumble = sides && !strcmp(sides, "two-sided-short-edge");
  const char *fmt = attr_str(as, "document-format");
  if (fmt && strcmp(fmt, "image/urf") && strcmp(fmt, "image/pwg-raster") &&
      strcmp(fmt, "application/octet-stream"))
    return ST_DOC_FORMAT;
  snprintf(j->format, sizeof j->format, "%s", fmt ? fmt : "application/octet-stream");
  return 0;
}

static job_t *new_job(req_t *q) {
  job_t *j = calloc(1, sizeof *j);
  const char *name = attr_str(&q->attrs, "job-name");
  const char *user = attr_str(&q->attrs, "requesting-user-name");
  snprintf(j->name, sizeof j->name, "%s", name ? name : "Untitled");
  snprintf(j->user, sizeof j->user, "%s", user ? user : "airprint");
  j->state = JS_PENDING;
  strcpy(j->reason, "none");
  j->created = time(NULL);
  return j;
}

static void queue_job(job_t *j) {
  pthread_mutex_lock(&jobs_lock);
  j->id = next_job_id++;
  snprintf(j->spool, sizeof j->spool, "%s/job-%d", cfg_spool, j->id);
  j->next = jobs;
  jobs = j;
  pthread_mutex_unlock(&jobs_lock);
}

/* stores the remaining request body as the job document */
static int spool_document(req_t *q, job_t *j) {
  int fd = open(j->spool, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) { logmsg("cannot create %s: %s", j->spool, strerror(errno)); return -1; }
  unsigned char buf[65536];
  ssize_t r;
  long long total = 0;
  while ((r = body_read(q->c, buf, sizeof buf)) > 0) {
    if (write_all(fd, buf, (size_t)r)) { close(fd); return -1; }
    total += r;
  }
  close(fd);
  if (r < 0) return -1;
  DBG("job %d: received %lld bytes (%s)", j->id, total, j->format);
  return 0;
}

static job_t *job_from_request(req_t *q) {
  int id = attr_int(&q->attrs, "job-id", -1);
  if (id < 0) {
    const char *juri = attr_str(&q->attrs, "job-uri");
    const char *s = juri ? strrchr(juri, '/') : NULL;
    if (s) id = atoi(s + 1);
  }
  return job_find(id);
}

static void handle_ipp(req_t *q) {
  ipp_attr *cs = attrs_find(&q->attrs, "attributes-charset");
  ipp_attr *nl = attrs_find(&q->attrs, "attributes-natural-language");
  if (!cs || !nl || q->reqid <= 0 || q->attrs.n < 2 ||
      strcmp(q->attrs.a[0].name, "attributes-charset") ||
      strcmp(q->attrs.a[1].name, "attributes-natural-language")) {
    resp_begin(q, ST_BAD_REQUEST);
    return;
  }
  if (strcasecmp(attr_str(&q->attrs, "attributes-charset"), "utf-8") &&
      strcasecmp(attr_str(&q->attrs, "attributes-charset"), "us-ascii")) {
    resp_begin(q, ST_BAD_CHARSET);
    return;
  }

  if (!attrs_find(&q->attrs, "printer-uri") && !attrs_find(&q->attrs, "job-uri")) {
    resp_begin(q, ST_BAD_REQUEST);
    return;
  }

  switch (q->op) {
  case OP_GET_PRINTER_ATTRIBUTES:
    resp_begin(q, ST_OK);
    printer_attrs(q);
    break;

  case OP_VALIDATE_JOB: {
    job_t tmp = {0};
    int st = apply_job_template(q, &tmp);
    resp_begin(q, st ? st : ST_OK);
    break;
  }

  case OP_PRINT_JOB:
  case OP_CREATE_JOB: {
    job_t *j = new_job(q);
    int st = apply_job_template(q, j);
    if (st) { free(j); body_drain(q->c); resp_begin(q, st); break; }
    queue_job(j);
    if (q->op == OP_PRINT_JOB) {
      if (spool_document(q, j)) {
        pthread_mutex_lock(&jobs_lock);
        j->state = JS_ABORTED; strcpy(j->reason, "aborted-by-system");
        pthread_mutex_unlock(&jobs_lock);
        resp_begin(q, ST_INTERNAL);
        break;
      }
      pthread_mutex_lock(&jobs_lock);
      j->have_doc = j->doc_spooled = 1;
      pthread_cond_signal(&jobs_cond);
      pthread_mutex_unlock(&jobs_lock);
    }
    logmsg("job %d: \"%s\" from %s (%s)", j->id, j->name, j->user, q->op == OP_PRINT_JOB ? "print" : "create");
    resp_begin(q, ST_OK);
    pthread_mutex_lock(&jobs_lock);
    job_attrs(q, j, 0);
    pthread_mutex_unlock(&jobs_lock);
    break;
  }

  case OP_SEND_DOCUMENT: {
    ipp_attr *ld = attrs_find(&q->attrs, "last-document");
    if (!ld || !ld->nvals || ld->vals[0].tag != TAG_BOOLEAN) {
      body_drain(q->c);
      resp_begin(q, ST_BAD_REQUEST);
      break;
    }
    int last = ld->vals[0].len == 1 && ld->vals[0].data[0];
    pthread_mutex_lock(&jobs_lock);
    job_t *j = job_from_request(q);
    int ok = j && j->state == JS_PENDING && !j->have_doc && !j->doc_spooled;
    pthread_mutex_unlock(&jobs_lock);
    if (!ok) { body_drain(q->c); resp_begin(q, j ? ST_NOT_POSSIBLE : ST_NOT_FOUND); break; }
    const char *fmt = attr_str(&q->attrs, "document-format");
    if (fmt) snprintf(j->format, sizeof j->format, "%s", fmt);
    if (spool_document(q, j)) { resp_begin(q, ST_INTERNAL); break; }
    pthread_mutex_lock(&jobs_lock);
    j->doc_spooled = 1;
    if (last || j->closed) { j->have_doc = 1; pthread_cond_signal(&jobs_cond); }
    resp_begin(q, ST_OK);
    job_attrs(q, j, 0);
    pthread_mutex_unlock(&jobs_lock);
    break;
  }

  case OP_CLOSE_JOB: {
    pthread_mutex_lock(&jobs_lock);
    job_t *j = job_from_request(q);
    if (j) {
      j->closed = 1;
      if (j->doc_spooled && !j->have_doc) { j->have_doc = 1; pthread_cond_signal(&jobs_cond); }
      else if (!j->doc_spooled && j->state == JS_PENDING) {
        j->state = JS_ABORTED; strcpy(j->reason, "aborted-by-system"); j->completed = time(NULL);
      }
    }
    pthread_mutex_unlock(&jobs_lock);
    resp_begin(q, j ? ST_OK : ST_NOT_FOUND);
    break;
  }

  case OP_CANCEL_JOB: {
    pthread_mutex_lock(&jobs_lock);
    job_t *j = job_from_request(q);
    int st = ST_OK;
    if (!j) st = ST_NOT_FOUND;
    else if (j->state >= JS_CANCELED) st = ST_NOT_POSSIBLE;
    else {
      j->cancel = 1;
      if (j->state == JS_PENDING) {
        j->state = JS_CANCELED; strcpy(j->reason, "job-canceled-by-user"); j->completed = time(NULL);
        unlink(j->spool);
      }
    }
    pthread_mutex_unlock(&jobs_lock);
    resp_begin(q, st);
    break;
  }

  case OP_CANCEL_MY_JOBS: {
    const char *me = attr_str(&q->attrs, "requesting-user-name");
    pthread_mutex_lock(&jobs_lock);
    for (job_t *j = jobs; j; j = j->next) {
      if (j->state >= JS_CANCELED || !me || strcmp(me, j->user)) continue;
      j->cancel = 1;
      if (j->state == JS_PENDING) {
        j->state = JS_CANCELED; strcpy(j->reason, "job-canceled-by-user"); j->completed = time(NULL);
        unlink(j->spool);
      }
    }
    pthread_mutex_unlock(&jobs_lock);
    resp_begin(q, ST_OK);
    break;
  }

  case OP_GET_JOB_ATTRIBUTES: {
    pthread_mutex_lock(&jobs_lock);
    job_t *j = job_from_request(q);
    if (!j) resp_begin(q, ST_NOT_FOUND);
    else { resp_begin(q, ST_OK); job_attrs(q, j, 1); }
    pthread_mutex_unlock(&jobs_lock);
    break;
  }

  case OP_GET_JOBS: {
    const char *which = attr_str(&q->attrs, "which-jobs");
    int limit = attr_int(&q->attrs, "limit", 1000);
    int completed = which && !strcmp(which, "completed");
    ipp_attr *myjobs = attrs_find(&q->attrs, "my-jobs");
    int mine = myjobs && myjobs->nvals && myjobs->vals[0].len == 1 && myjobs->vals[0].data[0];
    const char *me = attr_str(&q->attrs, "requesting-user-name");
    int all = which && !strcmp(which, "all");
    resp_begin(q, ST_OK);
    pthread_mutex_lock(&jobs_lock);
    int n = 0;
    for (job_t *j = jobs; j && n < limit; j = j->next) {
      int done = j->state >= JS_CANCELED;
      if (!all && done != completed) continue;
      if (mine && (!me || strcmp(me, j->user))) continue;
      if (!attrs_find(&q->attrs, "requested-attributes")) {
        /* RFC 8011 4.2.6: default is job-id and job-uri only */
        char uri[512], juri[600];
        printer_uri(q, uri, sizeof uri);
        snprintf(juri, sizeof juri, "%s/%d", uri, j->id);
        ob_u8(&q->resp, TAG_JOB);
        add_int(&q->resp, TAG_INTEGER, "job-id", j->id);
        add_str(&q->resp, TAG_URI, "job-uri", juri);
      } else job_attrs(q, j, 1);
      n++;
    }
    pthread_mutex_unlock(&jobs_lock);
    break;
  }

  case OP_IDENTIFY_PRINTER:
    logmsg("identify-printer requested");
    resp_begin(q, ST_OK);
    break;

  default:
    resp_begin(q, ST_OP_UNSUPPORTED);
    break;
  }
}

/* ------------------------------------------------------------------ */
/* HTTP                                                                */

static void http_send(conn_t *c, int code, const char *reason, const char *ctype,
                      const void *body, size_t len, int keep) {
  char hdr[512];
  int n = snprintf(hdr, sizeof hdr,
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Connection: %s\r\nServer: sp211-airprintd\r\n\r\n",
                   code, reason, ctype, len, keep ? "keep-alive" : "close");
  write_all(c->fd, hdr, (size_t)n);
  if (len) write_all(c->fd, body, len);
}

static void status_page(conn_t *c, int keep) {
  char html[4096];
  pthread_mutex_lock(&jobs_lock);
  int pstate = printer_state(), queued = queued_count();
  char reason[64];
  strcpy(reason, printer_reason);
  char rows[2048] = "";
  int n = 0;
  for (job_t *j = jobs; j && n < 10; j = j->next, n++) {
    static const char *names[] = {"", "", "", "pending", "held", "printing", "stopped",
                                  "canceled", "aborted", "completed"};
    char row[256];
    snprintf(row, sizeof row, "<tr><td>%d</td><td>%.60s</td><td>%.40s</td><td>%s</td></tr>",
             j->id, j->name, j->user, names[j->state]);
    strncat(rows, row, sizeof rows - strlen(rows) - 1);
  }
  pthread_mutex_unlock(&jobs_lock);
  int len = snprintf(html, sizeof html,
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>%s</title></head>"
    "<body style=\"font-family:-apple-system,sans-serif\"><h1>%s</h1>"
    "<p>State: %s%s &middot; queued jobs: %d</p>"
    "<table border=1 cellpadding=4><tr><th>Job</th><th>Name</th><th>User</th><th>State</th></tr>%s</table>"
    "</body></html>",
    cfg_name, cfg_name, pstate == 4 ? "printing" : "idle",
    strcmp(reason, "none") ? " (printer not connected)" : "", queued, rows);
  http_send(c, 200, "OK", "text/html; charset=utf-8", html, (size_t)len, keep);
}

static void *conn_thread(void *arg) {
  conn_t *c = arg;
  struct timeval tv = {120, 0};
  setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

  for (;;) {
    char line[2048], method[16] = "", path[1024] = "", host[256] = "";
    int keep = 1, expect = 0;
    c->chunked = 0; c->remaining = 0; c->body_done = 0;

    if (read_line(c, line, sizeof line) < 0) break;
    if (!line[0]) continue;
    if (sscanf(line, "%15s %1023s", method, path) != 2) break;
    if (strstr(line, "HTTP/1.0")) keep = 0;
    while (read_line(c, line, sizeof line) > 0) {
      char *colon = strchr(line, ':');
      if (!colon) continue;
      *colon = 0;
      char *v = colon + 1;
      while (*v == ' ') v++;
      if (!strcasecmp(line, "Content-Length")) c->remaining = strtoll(v, NULL, 10);
      else if (!strcasecmp(line, "Transfer-Encoding") && strcasestr(v, "chunked")) c->chunked = 1;
      else if (!strcasecmp(line, "Host")) snprintf(host, sizeof host, "%s", v);
      else if (!strcasecmp(line, "Connection") && !strcasecmp(v, "close")) keep = 0;
      else if (!strcasecmp(line, "Expect") && strcasestr(v, "100-continue")) expect = 1;
    }
    DBG("%s %s (host %s, %s)", method, path, host, c->chunked ? "chunked" : "length");

    if (!strcmp(method, "GET") || !strcmp(method, "HEAD")) {
      body_drain(c);
      if (!strcmp(path, "/icon.png")) http_send(c, 200, "OK", "image/png", icon_png, sizeof icon_png, keep);
      else if (!strcmp(path, "/") || !strncmp(path, "/ipp/print", 10)) status_page(c, keep);
      else http_send(c, 404, "Not Found", "text/plain", "not found\n", 10, keep);
      if (!keep) break;
      continue;
    }
    if (strcmp(method, "POST")) {
      body_drain(c);
      http_send(c, 405, "Method Not Allowed", "text/plain", "", 0, 0);
      break;
    }
    if (expect) write_all(c->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);

    req_t q;
    memset(&q, 0, sizeof q);
    q.c = c;
    snprintf(q.host, sizeof q.host, "%s", host);
    unsigned char h[8];
    if (body_exact(c, h, 8)) break;
    q.version = h[0] << 8 | h[1];
    q.op = h[2] << 8 | h[3];
    q.reqid = (int)be32(h + 4);
    int group = 0;
    if (parse_attrs(c, &q.attrs, &group, 0)) {
      DBG("malformed IPP request");
      http_send(c, 400, "Bad Request", "text/plain", "", 0, 0);
      attrs_free(&q.attrs);
      break;
    }
    DBG("IPP op 0x%04x id %d", q.op, q.reqid);
    if (h[0] < 1 || h[0] > 2) resp_begin(&q, ST_VERSION);
    else handle_ipp(&q);
    body_drain(c);
    ob_u8(&q.resp, TAG_END);
    http_send(c, 200, "OK", "application/ipp", q.resp.d, q.resp.len, keep);
    lilo2_buf_free(&q.resp);
    attrs_free(&q.attrs);
    if (!keep) break;
  }
  close(c->fd);
  free(c);
  return NULL;
}

/* ------------------------------------------------------------------ */

static void make_uuid(void) {
  if (cfg_uuid[0]) return;
  char host[256] = "sp211";
  gethostname(host, sizeof host);
  uint32_t hsh[4] = {0x811c9dc5u, 0x01000193u, 0x12345678u, 0x9e3779b9u};
  for (char *p = host; *p; p++)
    for (int i = 0; i < 4; i++) hsh[i] = (hsh[i] ^ (unsigned char)*p) * (0x01000193u + 2 * i);
  snprintf(cfg_uuid, sizeof cfg_uuid, "%08x-%04x-4%03x-a%03x-%04x%08x", hsh[0], hsh[1] >> 16,
           hsh[1] & 0xfff, hsh[2] >> 20, hsh[2] & 0xffff, hsh[3]);
}

int main(int argc, char *argv[]) {
  int opt;
  while ((opt = getopt(argc, argv, "p:d:s:n:u:l:v")) != -1) {
    switch (opt) {
    case 'p': cfg_port = atoi(optarg); break;
    case 'd': cfg_device = optarg; break;
    case 's': cfg_spool = optarg; break;
    case 'n': cfg_name = optarg; break;
    case 'u': snprintf(cfg_uuid, sizeof cfg_uuid, "%s", optarg); break;
    case 'l': cfg_listen = optarg; break;
    case 'v': cfg_verbose = 1; break;
    default:
      fprintf(stderr, "usage: %s [-p port] [-d device] [-s spooldir] [-n name] [-u uuid] [-l addr] [-v]\n", argv[0]);
      return 1;
    }
  }
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stderr, NULL, _IOLBF, 0);
  start_time = time(NULL);
  make_uuid();
  mkdir(cfg_spool, 0700);

  int ls = socket(AF_INET6, SOCK_STREAM, 0);
  int on = 1, off = 0;
  struct sockaddr_in6 a6 = {0};
  struct sockaddr_in a4 = {0};
  struct sockaddr *sa;
  socklen_t salen;
  if (cfg_listen && strchr(cfg_listen, '.')) {
    if (ls >= 0) close(ls);
    ls = socket(AF_INET, SOCK_STREAM, 0);
    a4.sin_family = AF_INET;
    a4.sin_port = htons((uint16_t)cfg_port);
    inet_pton(AF_INET, cfg_listen, &a4.sin_addr);
    sa = (struct sockaddr *)&a4; salen = sizeof a4;
  } else if (ls >= 0) {
    setsockopt(ls, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    a6.sin6_family = AF_INET6;
    a6.sin6_port = htons((uint16_t)cfg_port);
    if (cfg_listen) inet_pton(AF_INET6, cfg_listen, &a6.sin6_addr);
    else a6.sin6_addr = in6addr_any;
    sa = (struct sockaddr *)&a6; salen = sizeof a6;
  } else {
    ls = socket(AF_INET, SOCK_STREAM, 0);
    a4.sin_family = AF_INET;
    a4.sin_port = htons((uint16_t)cfg_port);
    a4.sin_addr.s_addr = htonl(INADDR_ANY);
    sa = (struct sockaddr *)&a4; salen = sizeof a4;
  }
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  if (ls < 0 || bind(ls, sa, salen) || listen(ls, 16)) {
    logmsg("cannot listen on port %d: %s", cfg_port, strerror(errno));
    return 1;
  }
  logmsg("%s listening on port %d, device %s, uuid %s", cfg_name, cfg_port, cfg_device, cfg_uuid);

  pthread_t wt;
  pthread_create(&wt, NULL, worker, NULL);

  for (;;) {
    int fd = accept(ls, NULL, NULL);
    if (fd < 0) { if (errno == EINTR) continue; sleep(1); continue; }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    conn_t *c = calloc(1, sizeof *c);
    c->fd = fd;
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 256 * 1024);
    if (pthread_create(&t, &at, conn_thread, c)) { close(fd); free(c); }
    pthread_attr_destroy(&at);
  }
}
