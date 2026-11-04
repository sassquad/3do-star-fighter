/* The internal resolution scale (SF_SCALE / SCALE in config.txt).  This
 * program runs at scale 2:
 *
 * - 2D cels (axis-aligned, at whole logical pixels: HUD, menus, text) drawn
 *   at scale 2 are exactly the scale-1 picture blown up, including blending
 *   with the frame buffer and frame-buffer-source cels (screen transitions).
 *   Scale 1 comes from the frozen reference renderer (tests/ref/sf_cel_ref.c).
 * - The cel builders give exactly the same CCB fields for a quad given in
 *   sub-pixel units (cel_subpix, the 3D view above scale 1) as for the same
 *   quad in whole pixels, so CCB magnitudes do not depend on the scale.
 * - pex_fix leaves the perspective tables alone at scale 1. */
#include "sft.h"
#include "sf_compat.h"
#include "sf_videosys.h"
#include "sf_shade_tables.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"
#include "Setup_Tables.h"
#include "../ref/sf_cel_ref.c"

#define W SF_SCREEN_W
#define H SF_SCREEN_H
#define S 2
#define MAXCELS 6

static uint64_t rng = 0x2545f4914f6cdd1dull;

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

static uint16_t small[2][W * H]; /* scale-1 frame buffers */
static uint32_t data[MAXCELS][64 * 64 + 8];
static uint32_t plut[MAXCELS][16];

/* frame buffer n (scale 2) against small[n] blown up: number of differences */
static int
compare_blown_up(int n, int *at)
{
  const uint16_t *big = video_buffer(n);
  int x, y, bad = 0;
  for (y = 0; y < H * S; y++) {
    for (x = 0; x < W * S; x++) {
      if (big[y * W * S + x] != small[n][(y / S) * W + x / S]) {
        if (!bad) {
          *at = y * W * S + x;
        }
        bad++;
      }
    }
  }
  return bad;
}

static uint32_t
some_pixc(void)
{
  switch (rint_(0, 4)) {
    case 0:
      return 0x1f001f00u; /* plain copy */
    case 1:
      return arm_pixcshade_type1[rint_(0, 31)];
    case 2:
      return arm_pixcshade_type2[rint_(0, 10)];
    case 3:
      return arm_pixcshade_type3[rint_(0, 10)]; /* adds the frame buffer */
    default:
      return arm_pixcshade_type4[rint_(0, 10)];
  }
}

/* One random list of axis-aligned cels at whole logical pixels: uncoded
   16 bpp or coded 4 bpp, magnified 1-3x, possibly flipped, partly off the
   screen; sometimes the other frame buffer as the source, copied 1:1. */
static int
one_case(void)
{
  CCB cels[MAXCELS];
  int count = rint_(1, MAXCELS), i, k, at = 0, bad;
  for (k = 0; k < 2; k++) {
    for (i = 0; i < W * H; i++) {
      small[k][i] = (uint16_t) (rnd() & 0x7fff);
    }
    video_upscale(video_buffer(k), small[k]);
  }
  for (i = 0; i < count; i++) {
    CCB *c = &cels[i];
    int w = rint_(1, 40), h = rint_(1, 40), mx = rint_(1, 3), my = rint_(1, 3);
    memset(c, 0, sizeof *c);
    c->ccb_Flags = CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_YOXY | CCB_CCBPRE |
                   (rint_(0, 3) == 0 ? CCB_BGND : 0);
    c->ccb_XPos = rint_(-30, W + 10) << 16;
    c->ccb_YPos = rint_(-30, H + 10) << 16;
    c->ccb_HDX = (rint_(0, 3) ? mx : -mx) << 20;
    c->ccb_VDY = (rint_(0, 3) ? my : -my) << 16;
    c->ccb_PIXC = some_pixc();
    if (rint_(0, 6) == 0) { /* the other frame buffer, 1:1, as in the blur transition */
      w = rint_(1, W);
      h = rint_(1, H);
      c->ccb_XPos = rint_(-20, 20) << 16;
      c->ccb_YPos = rint_(-20, 20) << 16;
      c->ccb_HDX = 1 << 20;
      c->ccb_VDY = 1 << 16;
      c->ccb_SourcePtr = (CelData *) video_buffer(1);
      c->ccb_PRE0 = ((uint32_t) (h - 1) << 6) | 0x10 | 6;
      c->ccb_PRE1 = (uint32_t) (w - 1) | (158u << 16);
    } else if (rint_(0, 1)) { /* uncoded 16 bpp, two pixels per word */
      int words = (w + 1) / 2;
      for (k = 0; k < words * h; k++) {
        data[i][k] = rint_(0, 5) ? rnd() & 0x7fff7fffu : 0; /* some transparent */
      }
      c->ccb_SourcePtr = (CelData *) data[i];
      c->ccb_PRE0 = ((uint32_t) (h - 1) << 6) | 0x10 | 6;
      c->ccb_PRE1 = (uint32_t) (w - 1) | ((uint32_t) (words - 2 < 0 ? 0 : words - 2) << 16);
      if (words < 2) { /* rows are at least two words */
        for (k = h - 1; k >= 0; k--) {
          data[i][2 * k] = data[i][k];
          data[i][2 * k + 1] = 0;
        }
      }
    } else { /* coded 4 bpp with a PLUT */
      int words = (w * 4 + 31) / 32;
      if (words < 2) {
        words = 2;
      }
      for (k = 0; k < words * h; k++) {
        data[i][k] = rnd();
      }
      for (k = 0; k < 16; k++) {
        plut[i][k] = rnd() & 0x7fff7fffu;
      }
      c->ccb_Flags |= CCB_LDPLUT;
      c->ccb_PLUTPtr = plut[i];
      c->ccb_SourcePtr = (CelData *) data[i];
      c->ccb_PRE0 = ((uint32_t) (h - 1) << 6) | 3;
      c->ccb_PRE1 = (uint32_t) (w - 1) | ((uint32_t) (words - 2) << 24);
    }
    c->ccb_NextPtr = (i + 1 < count) ? &cels[i + 1] : NULL;
  }
  cels[count - 1].ccb_Flags |= CCB_LAST;
  /* scale 1: reference renderer into small[0], with small[1] as the other buffer */
  {
    CCB copy[MAXCELS];
    memcpy(copy, cels, sizeof copy);
    for (i = 0; i < count; i++) {
      if (copy[i].ccb_SourcePtr == (CelData *) video_buffer(1)) {
        copy[i].ccb_SourcePtr = (CelData *) small[1];
      }
      copy[i].ccb_NextPtr = (i + 1 < count) ? &copy[i + 1] : NULL;
    }
    ref_draw_cels(small[0], copy, small[0], small[1]);
  }
  draw_cels(0, cels);
  bad = compare_blown_up(0, &at);
  if (bad) {
    printf("# %d of %d pixels differ, first at (%d,%d)\n", bad, W * H * S * S, at % (W * S),
           at / (W * S));
  }
  return bad;
}

TEST(test_2d_cels_are_blown_up_exactly)
{
  int n, failed = 0;
  for (n = 0; n < 400 && failed < 3; n++) {
    failed += one_case() != 0;
  }
  CHECK_EQ(failed, 0);
}

TEST(test_backdrop_and_fill_are_blown_up)
{
  static uint16_t img[W * H];
  int i, at = 0;
  for (i = 0; i < W * H; i++) {
    img[i] = (uint16_t) (rnd() & 0x7fff);
  }
  video_copy_in(0, img);
  memcpy(small[0], img, sizeof img);
  CHECK_EQ(compare_blown_up(0, &at), 0);
  video_fill(1, 0x1234);
  for (i = 0; i < W * H; i++) {
    small[1][i] = 0x1234;
  }
  CHECK_EQ(compare_blown_up(1, &at), 0);
}

/* ---- cel builders: sub-pixel units give the same CCB fields ---- */
static cel_celdata cq;
static CCB ccbs[2];
static CCB *plot[2];
static uint32_t game[8], pal[64];
static uint8_t tmap512[512 * 768];

static void
builder_setup(void)
{
  memset(&cq, 0, sizeof cq);
  plot[0] = &ccbs[0];
  plot[1] = &ccbs[1];
  cq.plotlist = plot;
  cq.cel_palette = (char *) pal;
  cq.cel_codedpalette = (char *) pal;
  cq.cel_map512 = (char *) tmap512;
  game[0] = 8; /* sprite 0 is 8 bytes in: PIXC, flags (no PLUT) */
  game[2] = 0x1f001f00u;
  game[3] = 0;
  cq.cel_game = (char *) game;
}

static void
put_quad(const int32_t *v, int sub)
{
  cq.x_pos0 = v[0] * (1 << sub);
  cq.y_pos0 = v[1] * (1 << sub);
  cq.x_pos1 = v[2] * (1 << sub);
  cq.y_pos1 = v[3] * (1 << sub);
  cq.x_pos2 = v[4] * (1 << sub);
  cq.y_pos2 = v[5] * (1 << sub);
  cq.x_pos3 = v[6] * (1 << sub);
  cq.y_pos3 = v[7] * (1 << sub);
}

/* build one cel of the given kind from quad v in whole pixels (ccbs[0]) and
   in sub-pixel units (ccbs[1]) */
static void
build_both(int kind, const int32_t *v, int zoom)
{
  int sub;
  for (sub = 0; sub <= 1; sub++) {
    cel_subpix = sub ? CEL_SUBPIX_BITS : 0;
    cq.temp_cels = sub;
    put_quad(v, cel_subpix);
    cq.shade = 5;
    if (kind == 0) {
      arm_addmonocel(&cq, 0, 1, 0);
    } else if (kind == 1) {
      arm_addcelfrom512map(&cq, 8, 8, zoom);
    } else {
      arm_addgamecel(&cq, 0, 1024 + zoom * 100, 1024);
    }
  }
  cel_subpix = 0;
}

TEST(test_subpixel_quads_encode_like_whole_pixels)
{
  int n, bad = 0;
  builder_setup();
  for (n = 0; n < 20000; n++) {
    int32_t v[8], k, kind = n % 3, zoom = rint_(0, 5);
    for (k = 0; k < 8; k++) {
      v[k] = rint_(-4000, 4000);
    }
    build_both(kind, v, zoom);
    if (ccbs[0].ccb_XPos != ccbs[1].ccb_XPos || ccbs[0].ccb_YPos != ccbs[1].ccb_YPos ||
        ccbs[0].ccb_HDX != ccbs[1].ccb_HDX || ccbs[0].ccb_HDY != ccbs[1].ccb_HDY ||
        ccbs[0].ccb_VDX != ccbs[1].ccb_VDX || ccbs[0].ccb_VDY != ccbs[1].ccb_VDY ||
        ccbs[0].ccb_HDDX != ccbs[1].ccb_HDDX || ccbs[0].ccb_HDDY != ccbs[1].ccb_HDDY) {
      if (bad++ < 3) {
        printf("# kind %d zoom %d: XPos %d/%d HDX %d/%d VDX %d/%d HDDX %d/%d\n", kind, zoom,
               ccbs[0].ccb_XPos, ccbs[1].ccb_XPos, ccbs[0].ccb_HDX, ccbs[1].ccb_HDX,
               ccbs[0].ccb_VDX, ccbs[1].ccb_VDX, ccbs[0].ccb_HDDX, ccbs[1].ccb_HDDX);
      }
    }
  }
  CHECK_EQ(bad, 0);
}

TEST(test_pex_fix)
{
  int32_t d;
  cel_subpix = 0;
  for (d = 1; d < (1 << 20); d += 997) {
    CHECK_EQ(pex_fix(d, 12345), 12345); /* scale 1: the table value */
  }
  cel_subpix = CEL_SUBPIX_BITS;
  CHECK_EQ(pex_fix(1023, 777), 777); /* below 1024: the table value */
  for (d = 1024; d < (1 << 20); d += 997) {
    CHECK_EQ(pex_fix(d, 1), 12582912 / d);
  }
  cel_subpix = 0;
}

int
main(void)
{
  if (!video_set_scale(S) || !video_open()) {
    printf("Bail out! cannot open video at scale %d\n", S);
    return 1;
  }
  RUN(test_2d_cels_are_blown_up_exactly);
  RUN(test_backdrop_and_fill_are_blown_up);
  RUN(test_subpixel_quads_encode_like_whole_pixels);
  RUN(test_pex_fix);
  return SFT_END();
}
