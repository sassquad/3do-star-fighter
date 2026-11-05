/* SF_ARMCell.c - C translation of SF_ARMCell.s
 *
 * Builds cel control blocks (CCBs) in the temporary plot list
 * (cel_quad.plotlist) and the 6-bit coded map / 4x4 tile cels that feed the
 * landscape.  All cel data is in the "word-swapped" model (see sf_compat.h), so
 * the shifts used to repack 6-bit pixels are identical to the original
 * big-endian ARM code.
 */
#include "SF_ARMCell.h"
#include "SF_Celutils.h"
#include "SF_Screenutils.h"
#include "sf_shade_tables.h"
#include "arm_ops.h"

#define cel_maxhold 1024
#define MAX_SHADE 31
#define MAX_SGAME 10
#define MIN_SHADE 0

/* CCB flag words from the original (DCD 2_...) */
#define CCBFLAG_MONOCHROME 0x3f664530u
#define CCBFLAG_4X4 0x3fa64430u
#define CCBFLAG_FROMMAP 0x3fe64430u

static int32_t arm_codedskipx;

int cel_subpix; /* see sf_compat.h */

/* Overflow pool for created 16x16 land cels (arm_add4cel4), reused every
   frame; only allocated for a medium grid larger than the default. */
static uint8_t *cel_overflow;
static int32_t cel_overflow_slots, cel_overflow_used;

void
arm_overflowcreation(int32_t slots)
{
  free(cel_overflow);
  cel_overflow = NULL;
  cel_overflow_slots = 0;
  if (slots > 0) {
    cel_overflow = (uint8_t *) calloc((size_t) slots + 1, 264);
    if (cel_overflow) {
      cel_overflow_slots = slots;
      arm_celinitialisecreation(cel_overflow, slots + 2); /* PRE0/PRE1 words */
    }
  }
}

static int32_t
clampshade(int32_t s, int32_t max)
{
  return s < MIN_SHADE ? MIN_SHADE : (s > max ? max : s);
}

static inline int32_t
SH(int32_t v, int32_t n)
{
  return (int32_t) LSL((uint32_t) v, (uint32_t) n);
}

/* SH for a screen coordinate (or difference) that may carry cel_subpix
   fractional bits: the CCB field gets the same fixed-point value as SH gives
   for whole pixels, so its magnitude does not depend on the scale.  Shifts
   smaller than cel_subpix drop the fraction the field cannot hold. */
static inline int32_t
SHP(int32_t v, int32_t n)
{
  n -= cel_subpix;
  return n >= 0 ? SH(v, n) : ASR(v, (uint32_t) -n);
}

/* Take the next CCB from the plot list, flushing the list first if it is full
 */
static CCB *
take_ccb(cel_celdata *cq)
{
  CCB *c;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  c = cq->plotlist[cq->temp_cels];
  cq->temp_cels++;
  return c;
}

/* The common quad set-up: XPos,YPos, HDX,HDY, VDX,VDY, HDDX,HDDY with per-axis
 * shifts */
static void
set_quad(CCB *c, const cel_celdata *q, int hs, int vs, int dds)
{
  int32_t x0 = q->x_pos0, y0 = q->y_pos0, x1 = q->x_pos1, y1 = q->y_pos1;
  int32_t x2 = q->x_pos2, y2 = q->y_pos2, x3 = q->x_pos3, y3 = q->y_pos3;
  c->ccb_XPos = (160 << 16) + SHP(x0, 16);
  c->ccb_YPos = (120 << 16) + SHP(y0, 16);
  c->ccb_HDX = SHP(x1 - x0, hs);
  c->ccb_HDY = SHP(y1 - y0, hs);
  c->ccb_VDX = SHP(x3 - x0, vs);
  c->ccb_VDY = SHP(y3 - y0, vs);
  c->ccb_HDDX = SHP(x2 - x3 - x1 + x0, dds);
  c->ccb_HDDY = SHP(y2 - y3 - y1 + y0, dds);
}

#define WORDS(p) ((const uint32_t *) (p))

void
arm_addpolycel16(void *celquad, int32_t sprite)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint32_t *list = WORDS(cq->cel_list16), *cel;
  CCB *c;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  cel = (const uint32_t *) ((const char *) list + list[sprite]);
  c = take_ccb(cq);
  c->ccb_PIXC = arm_pixcshade_type1[clampshade(cq->shade, MAX_SHADE)];
  c->ccb_Flags = cel[0];
  c->ccb_SourcePtr =
      (CelData *) (cel + 1 + 8); /* data follows the 32-byte palette */
  c->ccb_PLUTPtr = (void *) (cel + 1);
  set_quad(c, cq, 16, 12, 12);
}

void
arm_addpolycel32(void *celquad, int32_t sprite)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint32_t *list = WORDS(cq->cel_list32), *cel;
  const uint32_t *table;
  CCB *c;
  int32_t type, maxs, n;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  cel = (const uint32_t *) ((const char *) list + list[sprite]);
  c = take_ccb(cq);
  type = (int32_t) (cel[0] >> 8);
  maxs = (type == 0) ? MAX_SHADE : MAX_SGAME;
  if (type < 1) {
    table = arm_pixcshade_type1;
  } else if (type == 1) {
    table = arm_pixcshade_type3;
  } else {
    table = arm_pixcshade_type4;
  }
  c->ccb_PIXC = table[clampshade(cq->shade, maxs)];
  c->ccb_Flags = cel[1];
  c->ccb_SourcePtr = (CelData *) (cel + 2 + 8);
  c->ccb_PLUTPtr = (void *) (cel + 2);
  n = (int32_t) (cel[0] & 255); /* log2(size) - 2 */
  set_quad(c, cq, 18 - n, 14 - n, 16 - 2 * n);
}

void
arm_setpolycel32palette(void *celquad, int32_t sprite)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint32_t *list = WORDS(cq->cel_list32);
  if (cq->temp_cels - 1 < 0) {
    return;
  }
  cq->plotlist[cq->temp_cels - 1]->ccb_PLUTPtr =
      (void *) ((const char *) list + list[sprite] + 8);
}

void
arm_addgamecel(void *celquad, int32_t sprite, int32_t xscale, int32_t yscale)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint32_t *list = WORDS(cq->cel_game), *cel;
  CCB *c;
  uint32_t basepixc, flags;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  cel = (const uint32_t *) ((const char *) list + list[sprite]);
  basepixc = cel[0];
  flags = cel[1];
  cel += 2;
  c = take_ccb(cq);
  c->ccb_PIXC =
      basepixc | arm_pixcshade_type2[clampshade(cq->shade, MAX_SGAME)];
  c->ccb_Flags = flags;
  if (!(flags & (1u << 23))) {
    c->ccb_SourcePtr = (CelData *) cel; /* plut pointer left as it was */
  } else {
    c->ccb_SourcePtr = (CelData *) (cel + 16); /* 64 byte palette first */
    c->ccb_PLUTPtr = (void *) cel;
  }
  c->ccb_XPos = SHP(cq->x_pos0 + SUBPIX(160), 16);
  c->ccb_YPos = SHP(cq->y_pos0 + SUBPIX(120), 16);
  c->ccb_HDX = SH(xscale, 10);
  c->ccb_HDY = 0;
  c->ccb_VDX = 0;
  c->ccb_VDY = SH(yscale, 6);
  c->ccb_HDDX = 0;
  c->ccb_HDDY = 0;
}

void
arm_setgamecelpalette(void *celquad, int32_t sprite)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint32_t *list = WORDS(cq->cel_game);
  if (cq->temp_cels - 1 < 0) {
    return;
  }
  cq->plotlist[cq->temp_cels - 1]->ccb_PLUTPtr =
      (void *) ((const char *) list + list[sprite]);
}

/* Flat colour polygon: a 1x1 pixel cel stretched over the quad.
   colour = index into cel_palette (8 bytes per entry); type 0 normal, 1 mix, >1
   add */
void
arm_addmonocel(void *celquad, int32_t unused, int32_t colour, int32_t type)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  CCB *c;
  const uint32_t *table;
  int32_t maxs;
  (void) unused;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  c = take_ccb(cq);
  c->ccb_Flags = CCBFLAG_MONOCHROME;
  c->ccb_SourcePtr = (CelData *) (cq->cel_palette + colour * 8);
  if (type < 1) {
    table = arm_pixcshade_type1;
    maxs = MAX_SHADE;
  } else if (type == 1) {
    table = arm_pixcshade_type4;
    maxs = MAX_SGAME;
  } else {
    table = arm_pixcshade_type3;
    maxs = MAX_SGAME;
  }
  c->ccb_PIXC = table[clampshade(cq->shade, maxs)];
  set_quad(c, cq, 20, 16, 20);
  c->ccb_PRE0 = (1 << 4) + (1 << 3) + 6;
  c->ccb_PRE1 = 0;
}

void
arm_addmonocel_opaque(void *celquad, int32_t unused, int32_t colour)
{
  arm_addmonocel(celquad, unused, colour, 0);
}

/* Add a 16x16 cel built from four rows of four 4x4 tiles of the poly map at
 * (x,y) */
void
arm_add4cel4(void *celquad, void *polymap, int32_t x, int32_t y)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint8_t *map = (const uint8_t *) polymap;
  uint8_t *entry;
  uint8_t *creation = (uint8_t *) cq->cel_creation;
  uint32_t *source;
  CCB *c;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  entry = (uint8_t *) cq->cache_lookup + ((x >> 2) << 1) + ((y >> 2) << 7);
  if (entry[0] != 0) { /* cache hit */
    entry[0] = 2;
    source = (uint32_t *) (creation + entry[1] * 264);
  } else { /* cache miss: build the 16x16 cel */
    int32_t g, row;
    int32_t slot_count = cq->cache_freecount - 1;
    uint32_t *out;
    const uint32_t *tiles = WORDS(
        cq->cels4x4); /* tile n = 4 words at n*4: the .4 file has no header (the
                         asm's "+16" only offsets its pre-decrement reads) */
    const uint8_t *m;
    if (slot_count < 0) {
      /* Cache full.  A medium grid larger than the game's (LAND_MEDIUM in
         config.txt) can need more cels than the cache's 255 slots: build
         this one in the per-frame overflow pool instead, uncached.  Without
         a pool (the default size) the cel is left out, as it always was. */
      if (cel_overflow_used >= cel_overflow_slots) {
        return;
      }
      source = (uint32_t *) (cel_overflow + (size_t) cel_overflow_used++ * 264);
    } else {
      cq->cache_freecount = slot_count;
      entry[0] = 2;
      entry[1] = (uint8_t) cq->cache_free[slot_count];
      source = (uint32_t *) (creation + entry[1] * 264);
    }
    out = source + 2; /* skip PRE0/PRE1 */
    m = map + x + ((y + 3) << 8);
    for (g = 3; g >= 0; g--) {
      const uint32_t *t0 = tiles + m[0] * 4, *t1 = tiles + m[1] * 4,
                     *t2 = tiles + m[2] * 4, *t3 = tiles + m[3] * 4;
      uint32_t *o = out + g * 12;
      {
        const uint32_t *p0 = t0 + 4, *p1 = t1 + 4, *p2 = t2 + 4, *p3 = t3 + 4;
        for (row = 0; row < 4; row++) {
          uint32_t a = *--p0, b = *--p1, cc = *--p2, d = *--p3;
          *o++ = a | LSR(b, 24);
          *o++ = LSL(b, 8) | LSR(cc, 16);
          *o++ = LSL(cc, 16) | LSR(d, 8);
        }
      }
      m -= 256;
    }
  }
  c = take_ccb(cq);
  c->ccb_PIXC = arm_pixcshade_type1[clampshade(cq->shade, MAX_SHADE)];
  c->ccb_Flags = CCBFLAG_4X4;
  c->ccb_SourcePtr = (CelData *) source;
  c->ccb_PLUTPtr = (void *) cq->cel_codedpalette;
  set_quad(c, cq, 16, 12, 12);
}

/* Cels cut out of the pre-generated 6-bit maps.  zoom = log2 of the size
 * relative to base. */
static void
map_cel(cel_celdata *cq, const uint8_t *src, int32_t zoom, int hs, int vs,
        int dds, int32_t sizebits, int32_t woffset)
{
  CCB *c = take_ccb(cq);
  int32_t size = (1 << sizebits) - 1;
  c->ccb_Flags = CCBFLAG_FROMMAP;
  c->ccb_SourcePtr = (CelData *) src;
  c->ccb_PLUTPtr = (void *) cq->cel_codedpalette;
  c->ccb_PIXC = arm_pixcshade_type1[clampshade(cq->shade, MAX_SHADE)];
  set_quad(c, cq, hs, vs, dds);
  c->ccb_PRE0 = (1u << 30) | ((uint32_t) size << 6) | 4 |
                ((uint32_t) arm_codedskipx << 24);
  c->ccb_PRE1 = (uint32_t) (size + arm_codedskipx) | ((uint32_t) woffset << 24);
  (void) zoom;
}

void
arm_addcelfrom512map(void *celquad, int32_t x, int32_t y, int32_t zoom)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  int32_t x2 = x << 1, blocks;
  const uint8_t *src;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  arm_codedskipx = x2 & 15;
  blocks = x2 >> 4;
  src = (const uint8_t *) cq->cel_map512 + blocks * 12 + y * 768;
  map_cel(cq, src, zoom, 16 - zoom, 12 - zoom, 12 - 2 * zoom, zoom + 4, 94);
}

void
arm_addcelfrom128map(void *celquad, int32_t x, int32_t y, int32_t zoom)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  int32_t blocks;
  const uint8_t *src;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  x >>= 1;
  y >>= 1;
  arm_codedskipx = x & 15;
  blocks = x >> 4;
  src = (const uint8_t *) cq->cel_map128 + blocks * 12 + y * 96;
  map_cel(cq, src, zoom, 18 - zoom, 14 - zoom, 16 - 2 * zoom, zoom + 2, 22);
}

void
arm_addcelfrom32map(void *celquad, int32_t a, int32_t b, int32_t c_)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  CCB *c;
  (void) a;
  (void) b;
  (void) c_;
  if (cq->temp_cels >= cel_maxhold) {
    screen_intercept();
  }
  c = take_ccb(cq);
  c->ccb_Flags = CCBFLAG_FROMMAP;
  c->ccb_SourcePtr = (CelData *) cq->cel_map32;
  c->ccb_PLUTPtr = (void *) cq->cel_codedpalette;
  c->ccb_PIXC = arm_pixcshade_type1[clampshade(cq->shade, MAX_SHADE)];
  set_quad(c, cq, 15, 11, 10);
  c->ccb_PRE0 = (1u << 30) | (31u << 6) | 4;
  c->ccb_PRE1 = 31u | (4u << 24);
}

/* Header words for every created 16x16 cel (264 byte slots) */
void
arm_celinitialisecreation(void *buffer, int32_t count)
{
  uint32_t *p = (uint32_t *) buffer;
  int32_t n = count - 1;
  while (--n >= 0) {
    p[0] = (15u << 6) | 4;
    p[1] = (1u << 24) | (1u << 12) | 15;
    p = (uint32_t *) ((char *) p + 264);
  }
}

void
arm_initialisecache(void *celquad)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  int32_t i;
  memset(cq->cache_lookup, 0, 8192);
  cq->cache_freecount = 255;
  for (i = 254; i >= 0; i--) {
    cq->cache_free[i] = (char) i;
  }
}

/* Age the cache: entries not used this frame (flag 1) are freed, used ones (2)
 * drop to 1 */
void
arm_updatecache(void *celquad)
{
  cel_overflow_used = 0; /* a new frame: the overflow pool is free again */
  cel_celdata *cq = (cel_celdata *) celquad;
  uint8_t *lookup = (uint8_t *) cq->cache_lookup,
          *freel = (uint8_t *) cq->cache_free, *p;
  int32_t count = cq->cache_freecount;
  for (p = lookup + 8192 - 4; p >= lookup; p -= 4) {
    int r6;
    if (!(p[0] | p[1] | p[2] | p[3])) {
      continue;
    }
    r6 = p[2] - 1;
    if (r6 >= 0) {
      p[2] = (uint8_t) r6;
    }
    if (r6 == 0) {
      uint8_t slot = p[3];
      p[3] = 0;
      freel[count++] = slot;
    }
    r6 = p[0] - 1;
    if (r6 >= 0) {
      p[0] = (uint8_t) r6;
    }
    if (r6 != 0) {
      continue;
    }
    {
      uint8_t slot = p[1];
      p[1] = 0;
      freel[count++] = slot;
    }
  }
  cq->cache_freecount = count;
}

/* Build the 6-bit coded 512x512, 128x128 and 32x32 maps from the poly map */
static void
bitpack6(uint32_t *row, int idx, uint32_t v)
{
  int bit = idx * 6, w = bit >> 5, off = bit & 31;
  v &= 63;
  if (off <= 26) {
    row[w] |= v << (26 - off);
  } else {
    row[w] |= v >> (off - 26);
    row[w + 1] |= v << (32 - (off - 26));
  }
}

void
arm_generatemaps(void *celquad, void *polymap)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  const uint8_t *map = (const uint8_t *) polymap;
  const uint32_t *tiles = WORDS(cq->cels4x4);
  uint32_t *out;
  int x, y;

  out = (uint32_t *) cq->cel_map512;
  for (y = 0; y < 512; y++) {
    memset(out, 0, 96 * 4);
    for (x = 0; x < 256; x++) {
      const uint32_t *t = tiles + map[x + ((y >> 1) << 8)] * 4;
      uint32_t w = (y & 1) ? t[0] : t[2];
      bitpack6(out, 2 * x, w >> 26);
      bitpack6(out, 2 * x + 1, (w >> 14) & 63);
    }
    out += 96;
  }
  out = (uint32_t *) cq->cel_map128;
  for (y = 0; y < 128; y++) {
    memset(out, 0, 24 * 4);
    for (x = 0; x < 128; x++) {
      bitpack6(out, x, tiles[map[(x << 1) + (y << 9)] * 4] >> 26);
    }
    out += 24;
  }
  out = (uint32_t *) cq->cel_map32;
  for (y = 0; y < 32; y++) {
    memset(out, 0, 6 * 4);
    for (x = 0; x < 32; x++) {
      bitpack6(out, x, tiles[map[(x << 3) + (y << 11)] * 4] >> 26);
    }
    out += 6;
  }
}

/* Patch width / height in the preamble words of the last cel's source data */
void
arm_setcel_hw(void *celquad, int32_t width, int32_t height)
{
  cel_celdata *cq = (cel_celdata *) celquad;
  uint32_t *src;
  if (cq->temp_cels - 1 < 0) {
    return;
  }
  src = (uint32_t *) cq->plotlist[cq->temp_cels - 1]->ccb_SourcePtr;
  if (width - 1 >= 0) {
    src[1] = (src[1] & 0xfffff800u) | (uint32_t) (width - 1);
  }
  if (height - 1 >= 0) {
    src[0] = (src[0] & 0xffff003fu) | ((uint32_t) (height - 1) << 6);
  }
}

void
arm_interceptplot(void)
{
  screen_intercept();
}
