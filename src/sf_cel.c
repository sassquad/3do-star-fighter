/*
 * sf_cel.c - software emulation of the 3DO cel engine (the subset Star Fighter
 * uses).
 *
 * Cel pixel data is expected in the "word-swapped" model: 32-bit words whose
 * numeric value equals the big-endian word that was stored in the file / built
 * by the original ARM code.  Pixels are read MSB-first.
 *
 * Mapping model (matches the hardware):
 *      P(u,v) = P0 + v*V + u*(H + v*HDD)
 *   H,HDD are 12.20 fixed point per source pixel, V is 16.16 per source row,
 *   P0 is 16.16 screen position.  Each source texel is drawn as the
 * quadrilateral spanned by P(u,v) .. P(u+1,v+1) (forward mapping, later texels
 * overwrite earlier).
 *
 * Pixel processor (PIXC), per P-mode (low half = P-mode 0, high half = P-mode
 * 1): bits 12-10 MF  -> multiplier (MF+1) bits  9- 8 DV  -> divisor {16,2,4,8}
 *      bits  7- 6 2S  -> second source: 0 none, 2 frame buffer, 3 source pixel
 *      bits  5- 4 2DV -> second source divisor {1,2,4,8}
 *      bit      0     -> halve the sum
 *   result = clamp5( (src*(MF+1)/DV + src2/2DV) / (bit0?2:1) )
 *   (derived from the shade tables in SF_ARMCell.s whose comments give the
 * intended percentages; the tables reproduce those percentages exactly).
 */
#include "sf_compat.h"
#include "sf_videosys.h"
#include "sf_celtrace.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/* Cel engine                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
  uint8_t a[32], b[32];
  uint8_t shift; /* 1 if the sum is halved */
  uint8_t amv;   /* TextLib anti-alias mode (0xBFC0): bg * AMV/8 + pixel */
  uint8_t identity;
} PixLUT;

/* LUTs for the current PIXC word (index = P bit), rebuilt when it changes */
typedef struct {
  uint32_t pixc;
  int valid;
  PixLUT h[2];
} PixLUTPair;

typedef struct {
  uint16_t plut[32];
  uint32_t pixc;
  PixLUTPair lut;
  int32_t xpos, ypos, hdx, hdy, vdx, vdy, hddx, hddy;
} CEState;

#define PRE0_LITERAL 0x80000000u /* not needed by this renderer */
#define PRE0_BGND 0x40000000u    /* 000 pixels are drawn, not transparent */

#define MAXW 1024
#define MAXH 1024
/* longest row the rasteriser holds: a cel row, or a frame-buffer row at the
   largest scale */
#define MAXROW (SF_SCREEN_W * SF_MAX_SCALE > MAXW ? SF_SCREEN_W * SF_MAX_SCALE : MAXW)

/* Frame buffer size for the list being drawn (sf_fb_w x sf_fb_h).  Kept in
   file statics whose address is never taken, so the compiler knows the pixel
   stores cannot change them. */
static int g_w = SF_SCREEN_W, g_h = SF_SCREEN_H;

static inline uint32_t
get_bits(const uint32_t *row, uint32_t bitpos, int n)
{
  uint32_t wi = bitpos >> 5, off = bitpos & 31;
  /* Only read the following word if the field actually crosses into it: the
     last field of a row ends inside the row's final word, and row[wi + 1] may
     then lie past the end of the cel data (found by ASan on text cels). */
  uint64_t win = (uint64_t) row[wi] << 32;
  if (off + (uint32_t) n > 32) {
    win |= row[wi + 1];
  }
  return (uint32_t) ((win >> (64 - n - off)) & ((1u << n) - 1));
}

static inline int
is_fb_source(const CelData *p)
{
  return p == (const CelData *) video_buffer(0) || p == (const CelData *) video_buffer(1);
}

/* CCB_USEAV in force for the cel being drawn: 8bpp coded pixels carry a 3-bit
 * AMV in their top bits. */
static int g_useav;

static const int bpp_tab[8] = {0, 1, 2, 4, 6, 8, 16, 16};

/* Decode texel i of an unpacked source row as a 17-bit value: bit16 =
   transparent, bits15-0 colour (+P bit), plus the AMV in bits 19-17 for 8 bpp
   coded pixels under CCB_USEAV.  Texels are decoded only once their quad is
   known to cover a pixel centre (most texels of distant landscape do not). */
static inline uint32_t
decode_texel_unpacked(const uint32_t *row, int i, int bpp, int coded,
                      int plutpos, int skipx, const CEState *st, int force_p)
{
  uint32_t px = get_bits(row, (uint32_t) (i + skipx) * bpp, bpp), col, p, out;
  if (!coded) {
    col = px;
    p = (col >> 15) & 1;
  } else {
    uint32_t idx = px & ((bpp > 5) ? 31 : ((1u << bpp) - 1));
    col = st->plut[idx];
    if (!plutpos && bpp >= 6) {
      p = (px >> (bpp - 1)) & 1;
    } else {
      p = (col >> 15) & 1;
    }
  }
  if (force_p >= 0) {
    p = (uint32_t) force_p;
  }
  out = (col & 0x7fff) | (p << 15) | ((uint32_t) ((col & 0x7fff) == 0) << 16);
  if (g_useav && coded && bpp == 8) {
    out |= ((px >> 5) & 7) << 17; /* AMV */
  }
  return out;
}

/* Packed (RLE) rows. 'row' points at the row header. Returns pointer to next
 * row. */
/* Number of pixels in the row just decoded by decode_row_packed (natural-width
 * mode only). */
static int g_packed_px;

/* width > 0: the cel's PRE1 gave the width; pad/clip the row to it.
   width == 0: packed cel with no PRE1 (see draw_one); the row's own data says
   how long it is. */
static const uint32_t *
decode_row_packed(const uint32_t *row, int bpp, int coded, int plutpos,
                  int skipx, int width, const CEState *st, int force_p,
                  uint32_t *out)
{
  uint32_t pos, rowwords;
  int natural = (width == 0);
  int x = 0, total = natural ? MAXW - 1 : width + skipx;
  uint32_t tmp[MAXW + 64];
  if (bpp < 8) {
    rowwords = get_bits(row, 0, 8);
    pos = 8;
  } else {
    rowwords = get_bits(row, 0, 16);
    pos = 16;
  }
  while (x < total) {
    uint32_t hdr = get_bits(row, pos, 8), type = hdr >> 6, cnt = (hdr & 63) + 1,
             i;
    pos += 8;
    if (type == 0) {
      break;
    }
    if (x + (int) cnt > total) {
      cnt = (uint32_t) (total - x);
    }
    for (i = 0; i < cnt; i++) {
      uint32_t px = 0, col = 0, p, tr = 0;
      if (type == 1 || (type == 3 && i == 0)) {
        px = get_bits(row, pos, bpp);
        pos += bpp;
        if (type == 3) { /* remember for repeat */
          tmp[MAXW + 0] = px;
        }
      } else if (type == 3) {
        px = tmp[MAXW + 0];
      }
      if (type == 2) {
        tr = 1;
      } else if (!coded) {
        col = px;
        p = (col >> 15) & 1;
        (void) p;
        tr = (px & 0x7fff) == 0;
      } else {
        uint32_t idx = px & ((bpp > 5) ? 31 : ((1u << bpp) - 1));
        col = st->plut[idx];
        tr = (col & 0x7fff) == 0;
      }
      {
        uint32_t pp;
        if (!coded) {
          pp = (col >> 15) & 1;
        } else if (!plutpos && bpp >= 6) {
          pp = (px >> (bpp - 1)) & 1;
        } else {
          pp = (col >> 15) & 1;
        }
        if (force_p >= 0) {
          pp = (uint32_t) force_p;
        }
        tmp[x] = (col & 0x7fff) | (pp << 15) | (tr << 16);
      }
      x++;
    }
  }
  if (natural) {
    g_packed_px = (x > skipx) ? x - skipx : 0;
    if (g_packed_px) {
      memcpy(out, tmp + skipx, (size_t) g_packed_px * sizeof(uint32_t));
    }
    return (const uint32_t *) ((const uint8_t *) row + (rowwords + 2) * 4);
  }
  for (; x < total; x++) {
    tmp[x] = 1u << 16;
  }
  memcpy(out, tmp + skipx, (size_t) width * sizeof(uint32_t));
  return (const uint32_t *) ((const uint8_t *) row + (rowwords + 2) * 4);
}

/* Pixel processor, one table set per PIXC half (P-mode).  For each 5-bit
   channel value c of the cel pixel and b of the frame buffer the hardware
   computes  min(31, (c*MF/DV + second) >> halve)  with second = b/2DV (frame
   buffer), c/2DV (source) or 0.  a[] and b[] hold the two terms, so a pixel
   costs two table reads per channel instead of up to three divisions.  The
   arithmetic is the same integer arithmetic as before, done once per PIXC. */

static void
pixlut_build(PixLUT *l, uint32_t half)
{
  static const uint32_t dvt[4] = {16, 2, 4, 8};
  uint32_t mf = ((half >> 10) & 7) + 1, dv = dvt[(half >> 8) & 3], s2 = (half >> 6) & 3,
           d2 = 1u << ((half >> 4) & 3), c;
  l->amv = (half == 0xbfc0);
  l->shift = (uint8_t) (half & 1);
  l->identity = (half == 0x1f00); /* MF 8, DV 8, no second source: c */
  for (c = 0; c < 32; c++) {
    l->a[c] = (uint8_t) (c * mf / dv + (s2 == 3 ? c / d2 : 0));
    l->b[c] = (uint8_t) (s2 == 2 ? c / d2 : 0);
  }
}


static inline const PixLUT *
pixlut_for(PixLUTPair *pp, uint32_t pixc)
{
  if (!pp->valid || pp->pixc != pixc) {
    pixlut_build(&pp->h[0], pixc & 0xffff);
    pixlut_build(&pp->h[1], pixc >> 16);
    pp->pixc = pixc;
    pp->valid = 1;
  }
  return pp->h;
}

static inline uint16_t
pixproc(uint32_t pix16, const PixLUT *luts, uint16_t bg)
{
  const PixLUT *l = &luts[(pix16 >> 15) & 1];
  uint32_t r, g, b;
  if (l->identity) {
    return (uint16_t) (pix16 & 0x7fff);
  }
  if (l->amv) { /* TextLib anti-alias: existing pixel scaled by AMV/8, plus
                   the pixel's colour */
    uint32_t amv = (pix16 >> 17) & 7, o = 0, sh;
    for (sh = 0; sh <= 10; sh += 5) {
      uint32_t v = ((bg >> sh) & 31) * amv / 8 + ((pix16 >> sh) & 31);
      o |= (v > 31 ? 31 : v) << sh;
    }
    return (uint16_t) o;
  }
  r = (uint32_t) (l->a[(pix16 >> 10) & 31] + l->b[(bg >> 10) & 31]) >> l->shift;
  g = (uint32_t) (l->a[(pix16 >> 5) & 31] + l->b[(bg >> 5) & 31]) >> l->shift;
  b = (uint32_t) (l->a[pix16 & 31] + l->b[bg & 31]) >> l->shift;
  return (uint16_t) (((r > 31 ? 31 : r) << 10) | ((g > 31 ? 31 : g) << 5) | (b > 31 ? 31 : b));
}

/* for the unit tests: the pixel processor on one pixel */
uint16_t
sf_cel_pixproc(uint32_t pix, uint32_t pixc, uint16_t bg)
{
  PixLUTPair pp = {0, 0, {{{0}}}};
  return pixproc(pix, pixlut_for(&pp, pixc), bg);
}

static inline void
put_px(uint16_t *fb, int x, int y, uint32_t pix, const PixLUT *luts, int bgnd)
{
  uint16_t *d;
  if ((unsigned) x >= (unsigned) g_w || (unsigned) y >= (unsigned) g_h) {
    return;
  }
  if (((pix >> 16) & 1) && !bgnd) {
    return; /* transparent */
  }
  d = &fb[y * g_w + x];
  *d = pixproc(pix, luts, *d);
}

/* Fill the pixels whose centres lie inside the texel quad px/py (corners in
   drawing order).  x0..x1, y0..y1 is its pixel-centre bounding box, already
   known to be non-empty and to overlap the screen.  A one-pixel box is drawn
   without an inside test, as it always was.  The edge function is
       e = (px[j]-px[i])*(cy-py[i]) - (py[j]-py[i])*(cx-px[i])
   evaluated with exactly the same float operations as before; only the parts
   that do not change along a pixel row are computed once per row. */
/* Boxes at least this many pixels wide are filled as spans (fill_quad_box);
   narrower ones test each pixel centre.  Both give identical pixels. */
#define SPAN_MIN 8

/* One edge's value at pixel x of the row, exactly as fill_quad_box's
   per-pixel test computes it. */
static inline float
edge_at(float a, float ey, float px, int x)
{
  float cx = x + 0.5f;
  return a - ey * (cx - px);
}

static inline int
edge_ok(float a, float ey, float px, int x, int sense)
{
  float e = edge_at(a, ey, px, x);
  return sense > 0 ? e >= 0 : e <= 0;
}

/* Narrow [*lo, *hi] to the pixels where sense * edge >= 0.  The edge value
   is monotonic in x, so that set is a prefix or a suffix of the row (or all
   or nothing): start from the real-valued crossing and step to the exact
   integer end with the exact expression. */
static void
span_limit(float a, float ey, float px, int sense, int *lo, int *hi)
{
  int l = *lo, h = *hi, t;
  double g;
  if (l > h) {
    return;
  }
  if (ey == 0) { /* constant along the row */
    if (!edge_ok(a, ey, px, l, sense)) {
      *lo = h + 1;
    }
    return;
  }
  g = (double) px - 0.5 + (double) a / (double) ey; /* where the edge crosses 0 */
  if ((sense > 0) == (ey > 0)) { /* holds for x <= t: find the largest such x */
    t = g < l - 1 ? l - 1 : (g > h ? h : (int) floor(g));
    while (t < h && edge_ok(a, ey, px, t + 1, sense)) {
      t++;
    }
    while (t >= l && !edge_ok(a, ey, px, t, sense)) {
      t--;
    }
    *hi = t;
  } else { /* holds for x >= t: find the smallest such x */
    t = g < l ? l : (g > h + 1 ? h + 1 : (int) ceil(g));
    while (t > l && edge_ok(a, ey, px, t - 1, sense)) {
      t--;
    }
    while (t <= h && !edge_ok(a, ey, px, t, sense)) {
      t++;
    }
    *lo = t;
  }
}

static void
fill_quad_box(uint16_t *fb, const float *px, const float *py, int x0, int x1,
              int y0, int y1, uint32_t pix, const PixLUT *luts, int bgnd)
{
  float ex[4], ey[4], a[4];
  int i, x, y;
  if (x0 == x1 && y0 == y1) { /* common minified case */
    put_px(fb, x0, y0, pix, luts, bgnd);
    return;
  }
  if (x0 < 0) {
    x0 = 0;
  }
  if (y0 < 0) {
    y0 = 0;
  }
  if (x1 >= g_w) {
    x1 = g_w - 1;
  }
  if (y1 >= g_h) {
    y1 = g_h - 1;
  }
  if ((px[0] == px[3] && px[1] == px[2] && py[0] == py[1] && py[2] == py[3]) ||
      (px[0] == px[1] && px[2] == px[3] && py[0] == py[3] && py[1] == py[2])) {
    /* Axis-aligned rectangle (unrotated or 90-degree cels: text, HUD, sprites,
       screen transitions).  Every pixel centre of the bounding box lies inside
       or on its edges, where the edge functions are all >= 0 or all <= 0, so
       the inside test passes for all of them: fill the box. */
    for (y = y0; y <= y1; y++) {
      for (x = x0; x <= x1; x++) {
        put_px(fb, x, y, pix, luts, bgnd);
      }
    }
    return;
  }
  for (i = 0; i < 4; i++) {
    int j = (i + 1) & 3;
    ex[i] = px[j] - px[i];
    ey[i] = py[j] - py[i];
  }
  for (y = y0; y <= y1; y++) {
    float cy = y + 0.5f;
    for (i = 0; i < 4; i++) {
      a[i] = ex[i] * (cy - py[i]);
    }
    if (x1 - x0 >= SPAN_MIN) {
      /* Wide box (long thin texels such as the sky's one-texel-per-line
         strips once the view is rolled): find the covered pixels as spans
         instead of testing every centre.  A pixel is drawn iff all four edge
         values are >= 0 or all are <= 0.  Each edge value
         a - ey * ((x + 0.5f) - px) is monotonic in x (every float operation
         in it rounds monotonically), so "e >= 0" and "e <= 0" each hold on a
         prefix or a suffix of the row: the drawn pixels are the union of two
         intervals, whose ends are found with the same float expression. */
      int alo = x0, ahi = x1, blo = x0, bhi = x1;
      for (i = 0; i < 4 && (alo <= ahi || blo <= bhi); i++) {
        span_limit(a[i], ey[i], px[i], 1, &alo, &ahi);
        span_limit(a[i], ey[i], px[i], -1, &blo, &bhi);
      }
      if (alo > ahi) {
        alo = blo;
        ahi = bhi;
      } else if (blo <= bhi) {
        if (blo <= ahi + 1 && alo <= bhi + 1) { /* overlapping or adjacent: one span */
          alo = alo < blo ? alo : blo;
          ahi = ahi > bhi ? ahi : bhi;
        } else {
          for (x = blo; x <= bhi; x++) {
            put_px(fb, x, y, pix, luts, bgnd);
          }
        }
      }
      for (x = alo; x <= ahi; x++) {
        put_px(fb, x, y, pix, luts, bgnd);
      }
      continue;
    }
    for (x = x0; x <= x1; x++) {
      float cx = x + 0.5f;
      int neg = 0, pos = 0;
      for (i = 0; i < 4; i++) {
        float e = a[i] - ey[i] * (cx - px[i]);
        if (e < 0) {
          neg = 1;
        } else if (e > 0) {
          pos = 1;
        }
      }
      if (!(neg && pos)) {
        put_px(fb, x, y, pix, luts, bgnd);
      }
    }
  }
}

static inline int
min4i(int a, int b, int c, int d)
{
  int m = a < b ? a : b;
  m = m < c ? m : c;
  return m < d ? m : d;
}

static inline int
max4i(int a, int b, int c, int d)
{
  int m = a > b ? a : b;
  m = m > c ? m : c;
  return m > d ? m : d;
}

/* pixel-centre index of a coordinate: the first pixel whose centre is >= c */
static inline int
centre_ceil(float c)
{
  return (int) ceilf(c - 0.5f);
}

/* One cel, decoded from its CCB and the cel engine state: everything the
   rasteriser needs, as plain typed fields. */
typedef struct {
  const uint32_t *src; /* first row, after any preamble words */
  int fbsrc;           /* source is a frame buffer (16 bpp rows of sf_fb_w) */
  int packed;          /* rows are RLE packets */
  int natural;         /* packed without PRE1: each row gives its own width */
  int bpp, coded, plutpos, skipx;
  int force_p; /* -1, or the P-mode every pixel uses */
  int bgnd;    /* colour 000 is drawn rather than transparent */
  int width;   /* texels per row (0 when natural) */
  int height;
  int stride_words;     /* words per unpacked row */
  const CEState *st;    /* PLUT */
  const PixLUT *luts;   /* pixel processor for P-mode 0 and 1 */
  /* screen position of texel corner (u, v):
       (X, Y) + v * (Vx, Vy) + u * ((H0x, H0y) + v * (Dx, Dy)) */
  float X, Y, H0x, H0y, Vx, Vy, Dx, Dy;
} CelJob;

/* Apply a CCB to the engine state (the LD* flags load values that persist
   along the list) and describe the cel to draw.  Returns 0 if there is
   nothing to draw (SKIP, or a preamble the engine does not draw). */
static int
cel_prepare(const CCB *c, CEState *st, CelJob *j)
{
  uint32_t fl = c->ccb_Flags, pre0, pre1;
  const CelData *src = c->ccb_SourcePtr;
  int fbsrc = is_fb_source(src), natural = 0;

  if (fl & CCB_LDSIZE) {
    st->hdx = c->ccb_HDX;
    st->hdy = c->ccb_HDY;
    st->vdx = c->ccb_VDX;
    st->vdy = c->ccb_VDY;
  }
  if (fl & CCB_LDPRS) {
    st->hddx = c->ccb_HDDX;
    st->hddy = c->ccb_HDDY;
  }
  if (fl & CCB_LDPPMP) {
    st->pixc = c->ccb_PIXC;
  }
  g_useav = (fl & CCB_USEAV) != 0;
  if (fl & CCB_YOXY) {
    st->xpos = c->ccb_XPos;
    st->ypos = c->ccb_YPos;
  }
  if (fl & CCB_CCBPRE) {
    pre0 = c->ccb_PRE0;
    pre1 = c->ccb_PRE1;
  } else if ((fl & CCB_PACKED) && !fbsrc) {
    /* Packed cel with the preamble in its data: the data holds PRE0 only
       (confirmed against every packed cel in the game's Game.* files).  There
       is no PRE1, so the width comes from the rows. */
    pre0 = src[0];
    pre1 = 0;
    src += 1;
    natural = 1;
  } else {
    pre0 = src[0];
    pre1 = src[1];
    src += 2;
  }
  if ((fl & CCB_LDPLUT) && c->ccb_PLUTPtr) {
    const uint32_t *pw = (const uint32_t *) c->ccb_PLUTPtr;
    int i;
    for (i = 0; i < 32; i++) {
      st->plut[i] = (uint16_t) (pw[i >> 1] >> ((i & 1) ? 0 : 16));
    }
  }
  if (fl & CCB_SKIP) {
    return 0;
  }
  j->src = src;
  j->fbsrc = fbsrc;
  j->natural = natural;
  j->st = st;
  j->luts = pixlut_for(&st->lut, st->pixc);
  j->force_p = -1;
  if ((fl & CCB_POVER_MASK) == 0x100) {
    j->force_p = 0;
  } else if ((fl & CCB_POVER_MASK) == 0x180) {
    j->force_p = 1;
  }
  j->coded = !(pre0 & 0x10);
  j->bpp = bpp_tab[pre0 & 7];
  j->packed = (fl & CCB_PACKED) != 0;
  j->plutpos = (fl & CCB_PLUTPOS) != 0;
  j->skipx = (int) ((pre0 >> 24) & 15);
  /* PRE0 bit 31 is LITERAL and bit 30 is BGND (3DO Portfolio cel format);
     the preamble has no flip bits - cels are flipped by negative HDX/VDY.
     These bits were once taken for horizontal/vertical flips, which turned
     the 6-bit map land cels (far ring) upside down within each cell and
     mirrored the 32x32 polygon textures. */
  j->bgnd = (fl & CCB_BGND) || (pre0 & PRE0_BGND); /* draw 000 pixels */
  j->height = (int) ((pre0 >> 6) & 0x3ff) + 1;
  j->width = natural ? 0 : (int) (pre1 & 0x7ff) + 1 - j->skipx;
  if (j->bpp == 0 || (!natural && j->width <= 0) || j->width > MAXW || j->height > MAXH) {
    return 0;
  }
  if (natural) {
    j->stride_words = 0;
  } else if (j->bpp >= 8) {
    j->stride_words = (int) ((pre1 >> 16) & 0x3ff) + 2;
  } else {
    j->stride_words = (int) ((pre1 >> 24) & 0xff) + 2;
  }
  j->H0x = st->hdx / 1048576.0f;
  j->H0y = st->hdy / 1048576.0f;
  j->Vx = st->vdx / 65536.0f;
  j->Vy = st->vdy / 65536.0f;
  j->Dx = st->hddx / 1048576.0f;
  j->Dy = st->hddy / 1048576.0f;
  j->X = st->xpos / 65536.0f;
  j->Y = st->ypos / 65536.0f;
  if (sf_scale != 1) {
    /* The game positions everything on the 320x240 logical screen; draw it
       sf_scale times larger.  The engine state above stays logical (it
       carries over to later cels in the list); only this cel's geometry is
       scaled: P' = n * P for every texel corner. */
    const float n = (float) sf_scale;
    j->X *= n;
    j->Y *= n;
    if (fbsrc) {
      /* A frame buffer as source (screen transitions) is sf_scale times
         larger too: one texel per physical pixel, so with u' = n*u and
         v' = n*v the corners n * P(u, v) need X' = nX, H' = H, V' = V and
         D' = D/n. */
      j->width *= sf_scale;
      j->height *= sf_scale;
      j->Dx /= n;
      j->Dy /= n;
    } else {
      j->H0x *= n;
      j->H0y *= n;
      j->Vx *= n;
      j->Vy *= n;
      j->Dx *= n;
      j->Dy *= n;
    }
  }
  return 1;
}

/* Draw one prepared cel: each source texel is the quad spanned by its four
   corners; the pixels whose centres it covers get its colour through the
   pixel processor.  Texels are drawn in order, later ones over earlier ones. */
static void
cel_render(uint16_t *fb, const CelJob *j)
{
  const uint32_t *src = j->src;
  const CEState *st = j->st;
  const PixLUT *luts = j->luts;
  const int bpp = j->bpp, coded = j->coded, packed = j->packed, plutpos = j->plutpos;
  const int force_p = j->force_p, skipx = j->skipx, height = j->height, bgnd = j->bgnd;
  const int fbsrc = j->fbsrc, natural = j->natural, stride_words = j->stride_words;
  int width = j->width, v, u;
  uint32_t rowbuf[MAXROW + 8];
  {
    const float H0x = j->H0x, H0y = j->H0y, Vx = j->Vx, Vy = j->Vy, Dx = j->Dx, Dy = j->Dy;
    const float X = j->X, Y = j->Y;
    const uint32_t *rowp = src;
    for (v = 0; v < height; v++) {
      int vv = v, lazy = 0, u0 = 0;
      const uint32_t *rowdata = NULL;
      float Hx = H0x + vv * Dx, Hy = H0y + vv * Dy;
      float rx = X + vv * Vx, ry = Y + vv * Vy;
      float Hx2 = H0x + (vv + 1) * Dx, Hy2 = H0y + (vv + 1) * Dy;
      float rx2 = X + (vv + 1) * Vx, ry2 = Y + (vv + 1) * Vy;
      float tx0, ty0, bx0, by0;
      int ctx0, cty0, cbx0, cby0;
      if (!natural) {
        /* Whole-row rejection.  Along a row the corner coordinates
           rx + u*Hx etc. are monotonic in u (float multiply and add round
           monotonically), so the row's extreme corners are those at u = 0
           and u = width.  If they all fall between the same two pixel-centre
           rows (or columns), or off the screen, no texel of the row can
           cover a pixel centre: skip it without decoding. */
        int ya = centre_ceil(ry + u0 * Hy), yb = centre_ceil(ry + width * Hy);
        int yc = centre_ceil(ry2 + u0 * Hy2), yd = centre_ceil(ry2 + width * Hy2);
        int ymin = min4i(ya, yb, yc, yd), ymax = max4i(ya, yb, yc, yd);
        int skip = ymin == ymax || ymax <= 0 || ymin >= g_h;
        if (!skip) {
          int xa = centre_ceil(rx + u0 * Hx), xb = centre_ceil(rx + width * Hx);
          int xc = centre_ceil(rx2 + u0 * Hx2), xd = centre_ceil(rx2 + width * Hx2);
          int xmin = min4i(xa, xb, xc, xd), xmax = max4i(xa, xb, xc, xd);
          skip = xmin == xmax || xmax <= 0 || xmin >= g_w;
        }
        if (skip) {
          if (packed && !fbsrc) {
            rowp = (const uint32_t *) ((const uint8_t *) rowp +
                                       (get_bits(rowp, 0, bpp < 8 ? 8 : 16) + 2) * 4);
          } else if (!fbsrc) {
            rowp += stride_words;
          }
          continue;
        }
      }
      if (fbsrc) {
        const uint16_t *r = (const uint16_t *) src + (size_t) v * g_w;
        for (u = 0; u < width; u++) {
          rowbuf[u] = r[u] & 0xffff; /* never transparent */
        }
      } else if (packed) {
        rowp = decode_row_packed(rowp, bpp, coded, plutpos, skipx,
                                 natural ? 0 : width, st, force_p, rowbuf);
        if (natural) {
          width = g_packed_px; /* this row's pixel count */
        }
      } else {
        lazy = 1;
        rowdata = rowp;
        rowp += stride_words;
      }
      /* texel u spans corners u and u+1 of this row (top) and the next (bottom):
         P(u,v) = (rx + u*Hx, ry + u*Hy), P(u,v+1) = (rx2 + u*Hx2, ry2 + u*Hy2) */
      tx0 = rx + u0 * Hx;
      ty0 = ry + u0 * Hy;
      bx0 = rx2 + u0 * Hx2;
      by0 = ry2 + u0 * Hy2;
      ctx0 = centre_ceil(tx0);
      cty0 = centre_ceil(ty0);
      cbx0 = centre_ceil(bx0);
      cby0 = centre_ceil(by0);
      for (u = 0; u < width; u++) {
        int uu = u, x0, x1, y0, y1, ctx1, cty1, cbx1, cby1;
        float px[4], py[4];
        uint32_t pix;
        px[0] = tx0;
        py[0] = ty0;
        px[1] = rx + (uu + 1) * Hx;
        py[1] = ry + (uu + 1) * Hy;
        px[2] = rx2 + (uu + 1) * Hx2;
        py[2] = ry2 + (uu + 1) * Hy2;
        px[3] = bx0;
        py[3] = by0;
        /* corners u+1 are the next texel's corners u: keep them, and their
           pixel-centre indices */
        tx0 = px[1];
        ty0 = py[1];
        bx0 = px[2];
        by0 = py[2];
        ctx1 = centre_ceil(px[1]);
        cty1 = centre_ceil(py[1]);
        cbx1 = centre_ceil(px[2]);
        cby1 = centre_ceil(py[2]);
        /* bounding box of the pixel centres: ceil is monotonic, so taking it
           per corner gives the same box as taking it of the min / max */
        x0 = min4i(ctx0, ctx1, cbx1, cbx0);
        x1 = max4i(ctx0, ctx1, cbx1, cbx0) - 1;
        y0 = min4i(cty0, cty1, cby1, cby0);
        y1 = max4i(cty0, cty1, cby1, cby0) - 1;
        ctx0 = ctx1;
        cty0 = cty1;
        cbx0 = cbx1;
        cby0 = cby1;
        if (x1 < x0 || y1 < y0 || x1 < 0 || y1 < 0 || x0 >= g_w || y0 >= g_h) {
          continue; /* covers no pixel centre, or off screen */
        }
        pix = lazy ? decode_texel_unpacked(rowdata, u, bpp, coded, plutpos, skipx, st, force_p)
                   : rowbuf[u];
        if (((pix >> 16) & 1) && !bgnd) {
          continue;
        }
        fill_quad_box(fb, px, py, x0, x1, y0, y1, pix, luts, bgnd);
      }
    }
  }
}

void
draw_cels(int n, CCB *ccb)
{
  CEState st;
  uint16_t *fb = video_buffer(n);
  int trace = celtrace_active();
  if (trace) {
    celtrace_begin(n & 1, fb, ccb, video_buffer(0), video_buffer(1));
  }
  g_w = sf_fb_w;
  g_h = sf_fb_h;
  memset(&st, 0, sizeof st);
  st.pixc = 0x1f001f00u;
  while (ccb) {
    uint32_t fl = ccb->ccb_Flags;
    CelJob job;
    if (cel_prepare(ccb, &st, &job)) {
      cel_render(fb, &job);
    }
    if (fl & CCB_LAST) {
      break;
    }
    ccb = ccb->ccb_NextPtr;
  }
  if (trace) {
    celtrace_end(fb);
  }
}
