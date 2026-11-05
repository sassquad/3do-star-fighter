/* Differential test of the cel engine: thousands of random cel lists drawn by
 * the real renderer (src/sf_cel.c) and by the frozen reference copy
 * (tests/ref/sf_cel_ref.c) must give identical framebuffers.
 *
 * The random cels cover every pixel depth, coded/uncoded, packed with and
 * without a preamble in the CCB, PLUTPOS, BGND, P-mode override, USEAV, SKIP,
 * framebuffer sources, every PIXC word the game uses plus random ones, and
 * geometry from heavily minified to magnified, flipped, skewed and perspective
 * (HDD) quads, on and off the screen.  This is what lets the renderer be
 * optimised without re-recording any golden. */
#include "sft.h"
#include <math.h>
#include "sf_compat.h"
#include "sf_videosys.h"
#include "sf_shade_tables.h"
#include "../ref/sf_cel_ref.c"

#define W SF_SCREEN_W
#define H SF_SCREEN_H
#define MAXCELS 8
#define DATA_WORDS 8192

static uint64_t rng = 0x9e3779b97f4a7c15ull;

static uint32_t
rnd(void)
{
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return (uint32_t) (rng >> 16);
}

static int
rint_(int lo, int hi) /* inclusive */
{
  return lo + (int) (rnd() % (uint32_t) (hi - lo + 1));
}

static double
rfloat(double lo, double hi)
{
  return lo + (hi - lo) * (rnd() / 4294967296.0);
}

/* bit writer for building cel rows, MSB first into native words */
typedef struct {
  uint32_t *w;
  uint32_t pos;
} BW;

static void
put_bits(BW *b, uint32_t v, int n)
{
  int i;
  for (i = n - 1; i >= 0; i--) {
    if ((v >> i) & 1) {
      b->w[b->pos >> 5] |= 0x80000000u >> (b->pos & 31);
    }
    b->pos++;
  }
}

static const int bpp_codes[6] = {1, 2, 3, 4, 5, 6}; /* 1,2,4,6,8,16 bpp */
static const int bpp_of[8] = {0, 1, 2, 4, 6, 8, 16, 16};

static uint32_t
random_pixc(void)
{
  switch (rint_(0, 7)) {
  case 0:
    return 0x1f001f00u;
  case 1:
    return 0x1f00bfc0u; /* TextLib anti-alias */
  case 2:
    return arm_pixcshade_type2[rint_(0, 10)] | (rnd() & 0x00ff00ffu);
  case 3:
    return arm_pixcshade_type3[rint_(0, 10)];
  case 4:
    return arm_pixcshade_type4[rint_(0, 10)];
  case 5:
  case 6:
    return arm_pixcshade_type1[rint_(0, 31)];
  default:
    return rnd();
  }
}

static uint32_t
random_pixel(int bpp)
{
  /* bias towards 0 so transparency is exercised */
  if (rint_(0, 5) == 0) {
    return 0;
  }
  return rnd() & ((bpp == 16) ? 0xffffu : ((1u << bpp) - 1));
}

/* Fill 'data' with one cel's source data; returns the number of words used.
   Sets PRE0/PRE1 in the CCB or in the data according to CCB_CCBPRE. */
static int
make_cel_data(CCB *c, uint32_t *data, int maxwords)
{
  int code = bpp_codes[rint_(0, 5)], bpp = bpp_of[code];
  int coded = (bpp == 16) ? 0 : (rint_(0, 9) != 0);
  int packed = (c->ccb_Flags & CCB_PACKED) != 0;
  int ccbpre = (c->ccb_Flags & CCB_CCBPRE) != 0;
  int big = rint_(0, 7) == 0;
  int w = rint_(1, big ? 160 : 40), h = rint_(1, big ? 120 : 40);
  int skipx = rint_(0, 3) == 0 ? rint_(0, 7) : 0;
  uint32_t pre0, pre1;
  int words, v, x;
  memset(data, 0, (size_t) maxwords * 4);
  if (skipx >= w + skipx) {
    skipx = 0;
  }
  pre0 = ((uint32_t) skipx << 24) | ((uint32_t) (h - 1) << 6) | (coded ? 0 : 0x10) | (uint32_t) code;
  if (rint_(0, 3) == 0) {
    pre0 |= 0x40000000u; /* PRE0 BGND */
  }
  if (!packed) {
    int need = ((skipx + w) * bpp + 31) / 32, stride = need + rint_(0, 2);
    if (stride < 2) {
      stride = 2;
    }
    if (bpp >= 8) {
      pre1 = (uint32_t) (w - 1 + skipx) | ((uint32_t) (stride - 2) << 16);
    } else {
      pre1 = (uint32_t) (w - 1 + skipx) | ((uint32_t) (stride - 2) << 24);
    }
    words = ccbpre ? 0 : 2;
    if (words + stride * h + 2 > maxwords) {
      h = (maxwords - words - 2) / stride;
      pre0 = (pre0 & ~(0x3ffu << 6)) | ((uint32_t) (h - 1) << 6);
    }
    if (!ccbpre) {
      data[0] = pre0;
      data[1] = pre1;
    }
    for (v = 0; v < h; v++) {
      BW b = {data + words + v * stride, 0};
      for (x = 0; x < skipx + w; x++) {
        put_bits(&b, random_pixel(bpp), bpp);
      }
    }
    words += stride * h;
  } else {
    /* packed: rows of packets; with CCBPRE the width comes from PRE1, else the
       data carries PRE0 only and the rows give their own widths */
    pre1 = (uint32_t) (w - 1 + skipx);
    words = ccbpre ? 0 : 1;
    if (!ccbpre) {
      data[0] = pre0;
    }
    for (v = 0; v < h; v++) {
      uint32_t *row = data + words;
      BW b = {row, (bpp < 8) ? 8u : 16u};
      int total = rint_(0, skipx + w + 6), done = 0, rowwords;
      if (words + 8 + (total * (bpp + 8)) / 32 > maxwords) {
        h = v;
        break;
      }
      while (done < total) {
        int type = rint_(1, 3), cnt = rint_(1, 12), i;
        if (done + cnt > total) {
          cnt = total - done;
        }
        put_bits(&b, ((uint32_t) type << 6) | (uint32_t) (cnt - 1), 8);
        if (type == 1) {
          for (i = 0; i < cnt; i++) {
            put_bits(&b, random_pixel(bpp), bpp);
          }
        } else if (type == 3) {
          put_bits(&b, random_pixel(bpp), bpp);
        }
        done += cnt;
      }
      if (rint_(0, 1)) {
        put_bits(&b, 0, 8); /* explicit end packet */
      }
      rowwords = (int) ((b.pos + 31) / 32);
      if (rowwords < 2) {
        rowwords = 2;
      }
      rowwords += rint_(0, 1);
      /* row header: words in this row - 2 */
      if (bpp < 8) {
        row[0] = (row[0] & 0x00ffffffu) | ((uint32_t) (rowwords - 2) << 24);
      } else {
        row[0] = (row[0] & 0x0000ffffu) | ((uint32_t) (rowwords - 2) << 16);
      }
      words += rowwords;
    }
    if (h == 0) {
      h = 1;
      data[words] = 0;
      data[words + 1] = 0;
      words += 2;
    }
    pre0 = (pre0 & ~(0x3ffu << 6)) | ((uint32_t) (h - 1) << 6);
    if (!ccbpre) {
      data[0] = pre0;
    }
  }
  c->ccb_PRE0 = pre0;
  c->ccb_PRE1 = pre1;
  c->ccb_Width = w;
  c->ccb_Height = h;
  return words;
}

static int32_t
fx(double v, int frac)
{
  return (int32_t) (v * (double) (1 << frac));
}

static int force_style = -1; /* >= 0: always this geometry style */

static void
random_geometry(CCB *c)
{
  int style = force_style >= 0 ? force_style : rint_(0, 5);
  double hx, hy = 0, vx = 0, vy, ddx = 0, ddy = 0;
  switch (style) {
  case 6: { /* long thin texels at any angle: the sky's one texel per screen
               line, rolled (filled as spans, not pixel by pixel) */
    double ang = rfloat(0, 6.283185307), len = rfloat(8, 450), wid = rfloat(0.05, 2.5);
    hx = len * cos(ang);
    hy = len * sin(ang);
    vx = -wid * sin(ang) + rfloat(-0.3, 0.3);
    vy = wid * cos(ang) + rfloat(-0.3, 0.3);
    if (rint_(0, 2) == 0) { /* perspective, as the sky quads have */
      ddx = rfloat(-0.5, 0.5);
      ddy = rfloat(-0.5, 0.5);
    }
    if (rint_(0, 3) == 0) { /* or the long axis along V */
      double t = hx;
      hx = vx;
      vx = t;
      t = hy;
      hy = vy;
      vy = t;
    }
  } break;
  case 0: /* unit or integer scale, axis aligned */
    hx = rint_(1, 3) * (rint_(0, 3) ? 1 : -1);
    vy = rint_(1, 3) * (rint_(0, 3) ? 1 : -1);
    break;
  case 1: /* heavily minified (distant landscape) */
    hx = rfloat(0.02, 0.6);
    hy = rfloat(-0.2, 0.2);
    vx = rfloat(-0.3, 0.3);
    vy = rfloat(0.02, 0.6);
    ddx = rfloat(-0.01, 0.01);
    ddy = rfloat(-0.01, 0.01);
    break;
  case 2: /* magnified */
    hx = rfloat(-6, 6);
    hy = rfloat(-3, 3);
    vx = rfloat(-3, 3);
    vy = rfloat(-6, 6);
    break;
  case 3: /* perspective quads */
    hx = rfloat(-2, 2);
    hy = rfloat(-1, 1);
    vx = rfloat(-1, 1);
    vy = rfloat(-2, 2);
    ddx = rfloat(-0.1, 0.1);
    ddy = rfloat(-0.1, 0.1);
    break;
  case 4: /* exact halves and quarters (ties on pixel centres) */
    hx = rint_(-8, 8) / 4.0;
    hy = rint_(-4, 4) / 4.0;
    vx = rint_(-4, 4) / 4.0;
    vy = rint_(-8, 8) / 4.0;
    break;
  default: /* anything */
    hx = rfloat(-4, 4);
    hy = rfloat(-4, 4);
    vx = rfloat(-4, 4);
    vy = rfloat(-4, 4);
    ddx = rfloat(-0.3, 0.3);
    ddy = rfloat(-0.3, 0.3);
    break;
  }
  c->ccb_XPos = rint_(0, 3) ? fx(rfloat(-60, 380), 16) : (rint_(-60, 380) << 16) + (rint_(0, 1) << 15);
  c->ccb_YPos = rint_(0, 3) ? fx(rfloat(-60, 300), 16) : (rint_(-60, 300) << 16) + (rint_(0, 1) << 15);
  c->ccb_HDX = fx(hx, 20);
  c->ccb_HDY = fx(hy, 20);
  c->ccb_VDX = fx(vx, 16);
  c->ccb_VDY = fx(vy, 16);
  c->ccb_HDDX = fx(ddx, 20);
  c->ccb_HDDY = fx(ddy, 20);
}

static uint32_t datapool[MAXCELS][DATA_WORDS + 64];
static uint32_t plutpool[MAXCELS][16];
static uint16_t ref_fb[W * H];

static int
one_case(int n)
{
  CCB cels[MAXCELS];
  uint16_t *fb = video_buffer(0), *other = video_buffer(1);
  int count = rint_(1, MAXCELS), i, diffs = 0, first = -1;
  for (i = 0; i < W * H; i++) {
    fb[i] = (uint16_t) (rint_(0, 3) ? (rnd() & 0x7fff) : 0);
    other[i] = (uint16_t) (rnd() & 0xffff);
  }
  for (i = 0; i < count; i++) {
    CCB *c = &cels[i];
    uint32_t fl = CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_YOXY;
    memset(c, 0, sizeof *c);
    if (i > 0 && rint_(0, 4) == 0) {
      fl &= ~(rint_(0, 1) ? CCB_LDSIZE : CCB_YOXY); /* keep the previous cel's values */
    }
    if (rint_(0, 4)) {
      fl |= CCB_CCBPRE;
    }
    if (rint_(0, 2) == 0) {
      fl |= CCB_PACKED;
    }
    if (rint_(0, 4)) {
      fl |= CCB_LDPLUT;
    }
    if (rint_(0, 3) == 0) {
      fl |= CCB_PLUTPOS;
    }
    if (rint_(0, 5) == 0) {
      fl |= CCB_BGND;
    }
    if (rint_(0, 5) == 0) {
      fl |= rint_(0, 1) ? 0x100u : 0x180u; /* P-mode override */
    }
    if (rint_(0, 3) == 0) {
      fl |= CCB_USEAV;
    }
    if (rint_(0, 15) == 0) {
      fl |= CCB_SKIP;
    }
    c->ccb_Flags = fl;
    random_geometry(c);
    c->ccb_PIXC = random_pixc();
    {
      int k;
      for (k = 0; k < 16; k++) {
        plutpool[i][k] = rnd() & 0xffffffffu;
      }
    }
    c->ccb_PLUTPtr = plutpool[i];
    if (rint_(0, 20) == 0) {
      /* the other framebuffer as a 16 bpp source */
      int w = rint_(1, 320), h = rint_(1, 240);
      c->ccb_Flags = (fl | CCB_CCBPRE) & ~CCB_PACKED;
      c->ccb_SourcePtr = (CelData *) other;
      c->ccb_PRE0 = ((uint32_t) (h - 1) << 6) | 0x10 | 6;
      c->ccb_PRE1 = (uint32_t) (w - 1) | (158u << 16);
    } else {
      make_cel_data(c, datapool[i], DATA_WORDS);
      c->ccb_SourcePtr = (CelData *) datapool[i];
    }
    c->ccb_NextPtr = (i + 1 < count) ? &cels[i + 1] : (rint_(0, 1) ? NULL : &cels[0]);
  }
  cels[count - 1].ccb_Flags |= (cels[count - 1].ccb_NextPtr ? CCB_LAST : 0);
  memcpy(ref_fb, fb, sizeof ref_fb);
  ref_draw_cels(ref_fb, cels, video_buffer(0), other);
  draw_cels(0, cels);
  for (i = 0; i < W * H; i++) {
    if (fb[i] != ref_fb[i]) {
      if (first < 0) {
        first = i;
      }
      diffs++;
    }
  }
  if (diffs) {
    printf("# case %d: %d pixels differ, first at (%d,%d): got %04x want %04x\n", n, diffs,
           first % W, first / W, fb[first], ref_fb[first]);
  }
  return diffs == 0;
}

uint16_t sf_cel_pixproc(uint32_t pix, uint32_t pixc, uint16_t bg);

/* every PIXC half the game uses, plus random ones: all 32 x 32 channel values,
   both P-modes, every AMV value */
static int
pixproc_matches(uint32_t pixc)
{
  uint32_t c, b, p, amv;
  for (p = 0; p < 2; p++) {
    for (amv = 0; amv < 8; amv++) {
      for (c = 0; c < 32; c++) {
        for (b = 0; b < 32; b++) {
          uint32_t pix = (c * 0x421u) | (p << 15) | (amv << 17);
          uint16_t bg = (uint16_t) ((b << 10) | (((b + 7) & 31) << 5) | ((31 - b) & 31));
          pix = (pix & ~0x3e0u) | (((c * 5 + 3) & 31) << 5);
          if (sf_cel_pixproc(pix, pixc, bg) != pixproc(pix, pixc, bg)) {
            printf("# pixc %08x pix %05x bg %04x: got %04x want %04x\n", pixc, pix, bg,
                   sf_cel_pixproc(pix, pixc, bg), pixproc(pix, pixc, bg));
            return 0;
          }
        }
      }
    }
  }
  return 1;
}

TEST(test_pixel_processor_matches_reference)
{
  int i;
  for (i = 0; i < 32; i++) {
    CHECK(pixproc_matches(arm_pixcshade_type1[i]));
  }
  for (i = 0; i < 11; i++) {
    CHECK(pixproc_matches(arm_pixcshade_type2[i]));
    CHECK(pixproc_matches(arm_pixcshade_type3[i]));
    CHECK(pixproc_matches(arm_pixcshade_type4[i]));
  }
  CHECK(pixproc_matches(0x1f001f00u));
  CHECK(pixproc_matches(0x1f00bfc0u));
  for (i = 0; i < 300; i++) {
    CHECK(pixproc_matches(rnd()));
  }
}

TEST(test_random_cel_lists_match_reference)
{
  int n, bad = 0, cases = 1500;
  const char *e = getenv("SF_CELDIFF_CASES");
  if (e && atoi(e) > 0) {
    cases = atoi(e);
  }
  for (n = 0; n < cases && bad < 5; n++) {
    if (!one_case(n)) {
      bad++;
    }
  }
  CHECK_EQ(bad, 0);
}

/* Long thin rotated texels, as the sky draws when the view is rolled: the
   renderer fills wide pixel boxes as spans found from the edge functions;
   the reference tests every pixel centre. */
TEST(test_long_thin_texels_match_reference)
{
  int n, bad = 0;
  force_style = 6;
  for (n = 0; n < 600 && bad < 5; n++) {
    if (!one_case(100000 + n)) {
      bad++;
    }
  }
  force_style = -1;
  CHECK_EQ(bad, 0);
}

int
main(void)
{
  SFT_BEGIN();
  if (!video_open()) {
    printf("Bail out! video_open failed\n");
    return 1;
  }
  RUN(test_pixel_processor_matches_reference);
  RUN(test_random_cel_lists_match_reference);
  RUN(test_long_thin_texels_match_reference);
  return SFT_END();
}
