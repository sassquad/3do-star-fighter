/* sf_cinepak.c - Cinepak video decoder (written from the public format
 * description).
 *
 * Frame:  flags(1) length(3) width(2) height(2) strips(2)  [3DO files carry 6
 * extra bytes here] Strip:  id(2: 0x1000 intra / 0x1100 inter) size(2) y1(2)
 * x1(2) y2(2) x2(2), then chunks Chunk:  id(2) size(2, includes the 4 header
 * bytes): 0x20xx..0x27xx  codebook:  bit 0x01 = partial update, 0x02 = V1 (else
 * V4), 0x04 = greyscale 0x3000 intra vectors, 0x3100 inter vectors (with skip
 * bits), 0x3200 V1-only vectors Colour: R = Y + 2V, G = Y - U/2 - V, B = Y + 2U
 * (U,V signed 8-bit).
 */
#include "sf_cinepak.h"

#define MAX_STRIPS 32

typedef struct {
  uint8_t y[4];
  int8_t u, v;
} cv_entry;

typedef struct {
  cv_entry v1[256], v4[256];
} cv_book;

struct cinepak_ctx {
  int w, h;
  uint16_t *pix;
  cv_book book[MAX_STRIPS];
};

cinepak_ctx *
cinepak_create(int w, int h)
{
  cinepak_ctx *c = (cinepak_ctx *) calloc(1, sizeof *c);
  if (!c) {
    return NULL;
  }
  c->w = w;
  c->h = h;
  c->pix = (uint16_t *) calloc((size_t) w * h, sizeof(uint16_t));
  return c;
}

void
cinepak_destroy(cinepak_ctx *c)
{
  if (c) {
    free(c->pix);
    free(c);
  }
}

const uint16_t *
cinepak_pixels(const cinepak_ctx *c)
{
  return c->pix;
}

static uint32_t
rb16(const uint8_t *p)
{
  return (uint32_t) (p[0] << 8) | p[1];
}

static uint32_t
rb32(const uint8_t *p)
{
  return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static void
decode_codebook(cv_entry *cb, int id, const uint8_t *d, int size)
{
  const uint8_t *eod = d + size;
  int update = id & 0x0100, n = (id & 0x0400) ? 4 : 6, i;
  uint32_t flag = 0, mask = 0;
  for (i = 0; i < 256; i++) {
    if (update && !(mask >>= 1)) {
      if (d + 4 > eod) {
        return;
      }
      flag = rb32(d);
      d += 4;
      mask = 0x80000000u;
    }
    if (!update || (flag & mask)) {
      if (d + n > eod) {
        return;
      }
      cb[i].y[0] = d[0];
      cb[i].y[1] = d[1];
      cb[i].y[2] = d[2];
      cb[i].y[3] = d[3];
      if (n == 6) {
        cb[i].u = (int8_t) d[4];
        cb[i].v = (int8_t) d[5];
      } else {
        cb[i].u = 0;
        cb[i].v = 0;
      }
      d += n;
    }
  }
}

static inline uint16_t
yuv555(int y, int u, int v)
{
  int r = y + 2 * v, g = y - (u >> 1) - v, b = y + 2 * u;
  if (r < 0) {
    r = 0;
  }
  if (r > 255) {
    r = 255;
  }
  if (g < 0) {
    g = 0;
  }
  if (g > 255) {
    g = 255;
  }
  if (b < 0) {
    b = 0;
  }
  if (b > 255) {
    b = 255;
  }
  return (uint16_t) (((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static void
put2x2(uint16_t *dst, int stride, const cv_entry *e)
{
  dst[0] = yuv555(e->y[0], e->u, e->v);
  dst[1] = yuv555(e->y[1], e->u, e->v);
  dst[stride] = yuv555(e->y[2], e->u, e->v);
  dst[stride + 1] = yuv555(e->y[3], e->u, e->v);
}

/* one V1 vector covers a 4x4 block: each of its four luma values becomes a 2x2
 * pixel square */
static void
put4x4(uint16_t *dst, int stride, const cv_entry *e)
{
  int k;
  for (k = 0; k < 4; k++) {
    uint16_t p = yuv555(e->y[k], e->u, e->v);
    uint16_t *q = dst + (k >> 1) * 2 * stride + (k & 1) * 2;
    q[0] = q[1] = q[stride] = q[stride + 1] = p;
  }
}

static void
decode_vectors(cinepak_ctx *c, cv_book *bk, int id, const uint8_t *d, int size,
               int x1, int y1, int x2, int y2)
{
  const uint8_t *eod = d + size;
  uint32_t flag = 0, mask = 0;
  int x, y, stride = c->w;
  for (y = y1; y < y2; y += 4) {
    for (x = x1; x < x2; x += 4) {
      uint16_t *dst = c->pix + (size_t) y * stride + x;
      if (id == 0x3200) { /* V1 only, every block */
        if (d >= eod) {
          return;
        }
        put4x4(dst, stride, &bk->v1[*d++]);
        continue;
      }
      if ((id & 0x0100) && !(mask >>= 1)) { /* 0x3100: skip bits */
        if (d + 4 > eod) {
          return;
        }
        flag = rb32(d);
        d += 4;
        mask = 0x80000000u;
      }
      if (!(id & 0x0100) || (flag & mask)) {
        if (!(mask >>= 1)) {
          if (d + 4 > eod) {
            return;
          }
          flag = rb32(d);
          d += 4;
          mask = 0x80000000u;
        }
        if (flag & mask) { /* four V4 vectors */
          if (d + 4 > eod) {
            return;
          }
          put2x2(dst, stride, &bk->v4[d[0]]);
          put2x2(dst + 2, stride, &bk->v4[d[1]]);
          put2x2(dst + 2 * stride, stride, &bk->v4[d[2]]);
          put2x2(dst + 2 * stride + 2, stride, &bk->v4[d[3]]);
          d += 4;
        } else { /* one V1 vector */
          if (d >= eod) {
            return;
          }
          put4x4(dst, stride, &bk->v1[*d++]);
        }
      }
    }
  }
}

static void
decode_strip(cinepak_ctx *c, cv_book *bk, const uint8_t *d, int size, int x1,
             int y1, int x2, int y2)
{
  const uint8_t *eod = d + size;
  while (d + 4 <= eod) {
    int id = (int) rb16(d), csz = (int) rb16(d + 2);
    if (csz < 4 || d + csz > eod) {
      return;
    }
    switch (id) {
      case 0x2000:
      case 0x2100:
      case 0x2400:
      case 0x2500:
        decode_codebook(bk->v4, id, d + 4, csz - 4);
        break;
      case 0x2200:
      case 0x2300:
      case 0x2600:
      case 0x2700:
        decode_codebook(bk->v1, id, d + 4, csz - 4);
        break;
      case 0x3000:
      case 0x3100:
      case 0x3200:
        decode_vectors(c, bk, id, d + 4, csz - 4, x1, y1, x2, y2);
        break;
      default:
        break;
    }
    d += csz;
  }
}

int
cinepak_decode(cinepak_ctx *c, const uint8_t *data, size_t len)
{
  int flags, nstrips, i, y = 0;
  const uint8_t *p, *end = data + len;
  if (len < 16) {
    return -1;
  }
  flags = data[0];
  nstrips = (int) rb16(data + 8);
  if (nstrips < 1 || nstrips > MAX_STRIPS) {
    return -1;
  }
  p = data + 10;
  /* 3DO streams put six extra bytes before the first strip; find it explicitly
   */
  {
    int skip = 0;
    while (skip < 16 && p + skip + 12 <= end &&
           !(p[skip] == 0x10 || p[skip] == 0x11)) {
      skip++;
    }
    if (p[skip] != 0x10 && p[skip] != 0x11) {
      return -1;
    }
    p += skip;
  }
  for (i = 0; i < nstrips; i++) {
    int sid, ssz, sy1, sy2;
    if (p + 12 > end) {
      return -1;
    }
    sid = (int) rb16(p);
    ssz = (int) rb16(p + 2);
    sy1 = (int) rb16(p + 4);
    sy2 = (int) rb16(p + 8);
    if ((sid != 0x1000 && sid != 0x1100) || ssz < 12 || p + ssz > end) {
      return -1;
    }
    if (i > 0 && !(flags & 0x01)) {
      c->book[i] = c->book[i - 1];
    }
    {
      int h = sy2 - sy1;
      if (h <= 0 || y + h > c->h) {
        h = c->h - y;
      }
      decode_strip(c, &c->book[i], p + 12, ssz - 12, 0, y, c->w, y + h);
      y += h;
    }
    p += ssz;
  }
  return 0;
}
