/* sf_celtrace.c - record cel lists for replay (see sf_celtrace.h) */
#include "sf_celtrace.h"
#include "sf_testhooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_state = -1; /* -1 not initialised, 0 off, 1 on */
static FILE *g_out;
static uint32_t g_first = 1000, g_last = 1009;

static void
init(void)
{
  const char *p = getenv("SF_CELTRACE"), *r = getenv("SF_CELTRACE_FRAMES");
  g_state = 0;
  if (!p || !*p) {
    return;
  }
  if (r && *r) {
    unsigned a = 0, b = 0;
    int n = sscanf(r, "%u-%u", &a, &b);
    if (n == 1) {
      b = a;
    }
    if (n < 1 || b < a) {
      fprintf(stderr, "SF_CELTRACE_FRAMES: expected first-last, got '%s'\n", r);
      exit(2);
    }
    g_first = a;
    g_last = b;
  }
  g_out = fopen(p, "wb");
  if (!g_out) {
    fprintf(stderr, "SF_CELTRACE: cannot write '%s'\n", p);
    exit(2);
  }
  if (sf_scale == 1) {
    fwrite(CELTRACE_MAGIC, 1, 8, g_out);
  } else {
    uint32_t n = (uint32_t) sf_scale;
    fwrite(CELTRACE_MAGIC2, 1, 8, g_out);
    fwrite(&n, 4, 1, g_out);
  }
  g_state = 1;
}

int
celtrace_active(void)
{
  uint32_t f;
  if (g_state < 0) {
    init();
  }
  if (g_state == 0) {
    return 0;
  }
  f = th_frames();
  if (f > g_last) {
    fclose(g_out);
    g_out = NULL;
    g_state = 0;
    return 0;
  }
  return f >= g_first;
}

static void
w32(uint32_t v)
{
  fwrite(&v, 4, 1, g_out);
}

static const int bpp_tab[8] = {0, 1, 2, 4, 6, 8, 16, 16};

static uint32_t
first_bits(const uint32_t *row, int n)
{
  return row[0] >> (32 - n);
}

size_t
celtrace_source_bytes(const CCB *c, const uint32_t *src)
{
  uint32_t fl = c->ccb_Flags, pre0, pre1;
  size_t pre_bytes = 0;
  int natural = 0, bpp, height, width, skipx, v;
  if (!src) {
    return 0;
  }
  if (fl & CCB_CCBPRE) {
    pre0 = c->ccb_PRE0;
    pre1 = c->ccb_PRE1;
  } else if (fl & CCB_PACKED) {
    pre0 = src[0];
    pre1 = 0;
    pre_bytes = 4;
    natural = 1;
  } else {
    pre0 = src[0];
    pre1 = src[1];
    pre_bytes = 8;
  }
  if (fl & CCB_SKIP) {
    return pre_bytes;
  }
  bpp = bpp_tab[pre0 & 7];
  skipx = (int) ((pre0 >> 24) & 15);
  height = (int) ((pre0 >> 6) & 0x3ff) + 1;
  width = natural ? 0 : (int) (pre1 & 0x7ff) + 1 - skipx;
  if (bpp == 0 || (!natural && width <= 0) || width > 1024) {
    return pre_bytes;
  }
  if (fl & CCB_PACKED) {
    const uint8_t *row = (const uint8_t *) src + pre_bytes;
    for (v = 0; v < height; v++) {
      uint32_t words = first_bits((const uint32_t *) row, bpp < 8 ? 8 : 16);
      row += (words + 2) * 4;
    }
    return (size_t) (row - (const uint8_t *) src);
  } else {
    size_t stride = (bpp >= 8) ? ((pre1 >> 16) & 0x3ff) + 2 : ((pre1 >> 24) & 0xff) + 2;
    size_t last = ((size_t) (skipx + width) * (size_t) bpp + 31) / 32;
    return pre_bytes + ((size_t) height - 1) * stride * 4 + last * 4;
  }
}

void
celtrace_begin(int fbidx, const uint16_t *fb, const CCB *list, const uint16_t *fb0,
               const uint16_t *fb1)
{
  const CCB *c;
  uint32_t n = 0;
  w32(CELTRACE_LIST);
  w32(th_frames());
  w32((uint32_t) fbidx);
  (void) fb;
  fwrite(fb0, 2, (size_t) sf_fb_w * sf_fb_h, g_out);
  fwrite(fb1, 2, (size_t) sf_fb_w * sf_fb_h, g_out);
  for (c = list; c; c = c->ccb_NextPtr) {
    n++;
    if (c->ccb_Flags & CCB_LAST) {
      break;
    }
  }
  w32(n);
  for (c = list; c; c = c->ccb_NextPtr) {
    const uint32_t *src = (const uint32_t *) c->ccb_SourcePtr;
    uint32_t f[14] = {c->ccb_Flags, (uint32_t) c->ccb_XPos, (uint32_t) c->ccb_YPos,
                      (uint32_t) c->ccb_HDX, (uint32_t) c->ccb_HDY, (uint32_t) c->ccb_VDX,
                      (uint32_t) c->ccb_VDY, (uint32_t) c->ccb_HDDX, (uint32_t) c->ccb_HDDY,
                      c->ccb_PIXC, c->ccb_PRE0, c->ccb_PRE1, (uint32_t) c->ccb_Width,
                      (uint32_t) c->ccb_Height};
    fwrite(f, 4, 14, g_out);
    if (!src) {
      w32(0);
      w32(0);
    } else if (src == (const uint32_t *) fb0 || src == (const uint32_t *) fb1) {
      w32(src == (const uint32_t *) fb0 ? 2 : 3);
      w32(0);
    } else {
      size_t bytes = (celtrace_source_bytes(c, src) + 3) & ~(size_t) 3;
      w32(1);
      w32((uint32_t) bytes);
      fwrite(src, 1, bytes, g_out);
    }
    if (c->ccb_PLUTPtr) {
      w32(1);
      fwrite(c->ccb_PLUTPtr, 4, 16, g_out);
    } else {
      w32(0);
    }
    if (c->ccb_Flags & CCB_LAST) {
      break;
    }
  }
}

void
celtrace_end(const uint16_t *fb)
{
  w32(CELTRACE_DONE);
  fwrite(fb, 2, (size_t) sf_fb_w * sf_fb_h, g_out);
  fflush(g_out);
}
