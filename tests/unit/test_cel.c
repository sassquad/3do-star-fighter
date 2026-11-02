/* sf_cel.c: the 3DO cel engine, driven with small hand-built cels and checked
 * pixel by pixel.  Includes regressions for the packed-cel preamble bug and the
 * get_bits read past the end of a cel (run under `make test-asan` to catch
 * the latter as a memory error). */
#include "sft.h"
#include "sf_compat.h"
#include "sf_testhooks.h"

static ScreenContext sc;
static uint16_t *fb;

#define PIXC_IDENTITY 0x1f001f00u
#define FLAGS_BASE                                                             \
  (CCB_LAST | CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_YOXY | CCB_CCBPRE)

static void
clear_fb(uint16_t v)
{
  int i;
  for (i = 0; i < 320 * 240; i++) {
    fb[i] = v;
  }
}

static uint16_t
px(int x, int y)
{
  return fb[y * 320 + x];
}

/* unit-scale cel at (x,y) */
static void
setup_ccb(CCB *c, int x, int y, int scale)
{
  memset(c, 0, sizeof *c);
  c->ccb_Flags = FLAGS_BASE;
  c->ccb_XPos = x << 16;
  c->ccb_YPos = y << 16;
  c->ccb_HDX = scale << 20;
  c->ccb_VDY = scale << 16;
  c->ccb_PIXC = PIXC_IDENTITY;
}

/* PRE0/PRE1 for an unpacked cel */
static void
set_pre(CCB *c, int bppcode, int coded, int w, int h, int words_per_row)
{
  c->ccb_PRE0 = ((uint32_t) (h - 1) << 6) | (coded ? 0 : 0x10) |
                (uint32_t) bppcode;
  if (bppcode >= 5) { /* 8, 16 bpp: 10-bit word offset at bit 16 */
    c->ccb_PRE1 = (uint32_t) (w - 1) | ((uint32_t) (words_per_row - 2) << 16);
  } else {
    c->ccb_PRE1 = (uint32_t) (w - 1) | ((uint32_t) (words_per_row - 2) << 24);
  }
  c->ccb_Width = w;
  c->ccb_Height = h;
}

TEST(test_uncoded16_unit_scale)
{
  static uint32_t data[2 * 2] = {
      (0x0000u << 16) | 0x7c00u, (0x03e0u << 16) | 0x001fu, /* row 0 */
      (0x1234u << 16) | 0x4321u, (0x0421u << 16) | 0x7fffu  /* row 1 */
  };
  CCB c;
  clear_fb(0x0005);
  setup_ccb(&c, 10, 20, 1);
  set_pre(&c, 6, 0, 4, 2, 2);
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(10, 20), 0x0005); /* colour 0 is transparent */
  CHECK_EQX(px(11, 20), 0x7c00);
  CHECK_EQX(px(12, 20), 0x03e0);
  CHECK_EQX(px(13, 20), 0x001f);
  CHECK_EQX(px(10, 21), 0x1234);
  CHECK_EQX(px(13, 21), 0x7fff);
  CHECK_EQX(px(14, 20), 0x0005); /* nothing outside the cel */
  CHECK_EQX(px(10, 22), 0x0005);
  CHECK_EQX(px(9, 20), 0x0005);
}

TEST(test_scale_and_flips)
{
  static uint32_t data[2] = {(0x1111u << 16) | 0x2222u, 0};
  CCB c;
  clear_fb(0);
  setup_ccb(&c, 50, 60, 2); /* each source pixel covers 2x2 */
  set_pre(&c, 6, 0, 2, 1, 2);
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(50, 60), 0x1111);
  CHECK_EQX(px(51, 61), 0x1111);
  CHECK_EQX(px(52, 60), 0x2222);
  CHECK_EQX(px(53, 61), 0x2222);
  CHECK_EQX(px(54, 60), 0);
  CHECK_EQX(px(50, 62), 0);
  clear_fb(0); /* PRE0 bit 31 is LITERAL, not a horizontal flip */
  setup_ccb(&c, 50, 60, 1);
  set_pre(&c, 6, 0, 2, 1, 2);
  c.ccb_PRE0 |= 1u << 31;
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(50, 60), 0x1111);
  CHECK_EQX(px(51, 60), 0x2222);
}

/* PRE0 bit 30 is BGND (draw 000 pixels), not a vertical flip: the far land
   ring sets it, and reading it as a flip turned each far cell upside down */
TEST(test_pre0_bgnd_not_flip)
{
  static uint32_t data[4] = {0x2AAA0000u | 0x0AAA, 0, 0x5555u << 16 | 0x0000,
                             0};
  CCB c;
  clear_fb(0x0007);
  setup_ccb(&c, 0, 0, 1);
  set_pre(&c, 6, 0, 2, 2, 2);
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(1, 1), 0x0007); /* 000 is transparent without BGND */
  clear_fb(0x0007);
  c.ccb_PRE0 |= 1u << 30;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), 0x2AAA); /* rows are not flipped */
  CHECK_EQX(px(0, 1), 0x5555);
  CHECK_EQX(px(1, 1), 0x0000); /* ...and the 000 pixel is drawn */
}

/* PLUT: 32 entries packed two per word, entry 2k in the high half */
static uint32_t plut[16];

static void
set_plut(int i, uint16_t v)
{
  if (i & 1) {
    plut[i >> 1] = (plut[i >> 1] & 0xffff0000u) | v;
  } else {
    plut[i >> 1] = (plut[i >> 1] & 0xffffu) | ((uint32_t) v << 16);
  }
}

TEST(test_coded_depths_use_plut)
{
  /* bpp code -> bits: 1->1, 2->2, 3->4, 4->6, 5->8 */
  static const int codes[] = {1, 2, 3, 4, 5}, bits[] = {1, 2, 4, 6, 8};
  int k, i;
  for (i = 0; i < 32; i++) {
    set_plut(i, (uint16_t) (0x0400 + i)); /* never 0: all opaque */
  }
  for (k = 0; k < 5; k++) {
    uint32_t data[2] = {0, 0}, pos = 0;
    int n = 4, maxv = (bits[k] >= 5 ? 31 : (1 << bits[k]) - 1);
    CCB c;
    for (i = 0; i < n; i++) { /* pixel values 0,1,2,3 (capped) */
      uint32_t v = (uint32_t) (i > maxv ? maxv : i);
      data[pos >> 5] |= v << (32 - bits[k] - (pos & 31));
      pos += (uint32_t) bits[k];
    }
    clear_fb(0);
    setup_ccb(&c, 100, 100, 1);
    set_pre(&c, codes[k], 1, n, 1, 2);
    c.ccb_Flags |= CCB_LDPLUT;
    c.ccb_PLUTPtr = plut;
    c.ccb_SourcePtr = (CelData *) data;
    DrawCels(sc.sc_BitmapItems[0], &c);
    for (i = 0; i < n; i++) {
      int v = i > maxv ? maxv : i;
      if ((px(100 + i, 100) & 0x7fff) != 0x0400 + v) {
        SFT_FAIL_("%d bpp pixel %d: 0x%04x, expected 0x%04x", bits[k], i,
                  px(100 + i, 100), 0x0400 + v);
      }
    }
  }
}

TEST(test_skipx)
{
  static uint32_t data[2] = {(0x1111u << 16) | 0x2222u, (0x3333u << 16)};
  CCB c;
  clear_fb(0);
  setup_ccb(&c, 0, 0, 1);
  set_pre(&c, 6, 0, 3, 1, 2);
  c.ccb_PRE0 |= 1u << 24; /* skip one source pixel */
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), 0x2222);
  CHECK_EQX(px(1, 0), 0x3333);
  CHECK_EQX(px(2, 0), 0);
}

TEST(test_pixc_scale_and_blend)
{
  static uint32_t data[2] = {(uint32_t) ((20 << 10) | (20 << 5) | 20) << 16,
                             0};
  CCB c;
  /* MF 4, DV 8: half intensity */
  clear_fb(0);
  setup_ccb(&c, 0, 0, 1);
  set_pre(&c, 6, 0, 1, 1, 2);
  c.ccb_SourcePtr = (CelData *) data;
  c.ccb_PIXC = 0x0F000F00u;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), (10 << 10) | (10 << 5) | 10);
  /* plus the frame buffer pixel divided by 2 */
  clear_fb((uint16_t) ((8 << 10) | (8 << 5) | 8));
  c.ccb_PIXC = 0x0F900F90u;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), (14 << 10) | (14 << 5) | 14);
  /* saturates at 31 */
  clear_fb(0x7fff);
  c.ccb_PIXC = 0x1F801F80u; /* MF 8, DV 8, + frame buffer */
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), 0x7fff);
}

/* ---- packed cels ---- */
typedef struct {
  uint32_t w[64];
  uint32_t bit;
} bitw;

static void
put(bitw *b, uint32_t v, int n)
{
  int i;
  for (i = n - 1; i >= 0; i--, b->bit++) {
    if ((v >> i) & 1) {
      b->w[b->bit >> 5] |= 1u << (31 - (b->bit & 31));
    }
  }
}

/* one packed 16bpp row: literal run of n pixels, then end of line */
static void
packed_row(bitw *b, const uint16_t *p, int n)
{
  uint32_t start = b->bit, words;
  int i;
  put(b, 0, 16); /* row length, patched below */
  put(b, (1u << 6) | (uint32_t) (n - 1), 8);
  for (i = 0; i < n; i++) {
    put(b, p[i], 16);
  }
  put(b, 0, 8); /* type 0: end of row */
  b->bit = (b->bit + 31) & ~31u;
  words = (b->bit - start) / 32;
  b->w[start >> 5] |= (words - 2) << 16;
}

/* Regression (PORTING_NOTES §9): a packed cel without CCB_CCBPRE carries only
   PRE0 in its data; the rows start straight after it. */
TEST(test_packed_without_ccbpre_has_one_preamble_word)
{
  static const uint16_t r0[3] = {0x1001, 0x1002, 0x1003},
                        r1[3] = {0x2001, 0x2002, 0x2003};
  bitw b;
  CCB c;
  memset(&b, 0, sizeof b);
  put(&b, (1u << 6) | 0x10 | 6, 32); /* PRE0: 2 rows, uncoded, 16 bpp */
  packed_row(&b, r0, 3);
  packed_row(&b, r1, 3);
  clear_fb(0);
  setup_ccb(&c, 30, 40, 1);
  c.ccb_Flags = (FLAGS_BASE & ~CCB_CCBPRE) | CCB_PACKED;
  c.ccb_SourcePtr = (CelData *) b.w;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(30, 40), 0x1001);
  CHECK_EQX(px(32, 40), 0x1003);
  CHECK_EQX(px(30, 41), 0x2001);
  CHECK_EQX(px(32, 41), 0x2003);
  CHECK_EQX(px(33, 40), 0);
}

TEST(test_packed_transparent_and_repeat)
{
  bitw b;
  CCB c;
  uint32_t start;
  memset(&b, 0, sizeof b);
  start = b.bit;
  put(&b, 0, 16);
  put(&b, (2u << 6) | 1, 8); /* 2 transparent */
  put(&b, (3u << 6) | 2, 8); /* repeat 3 times: */
  put(&b, 0x0abc, 16);
  put(&b, (1u << 6) | 0, 8); /* 1 literal */
  put(&b, 0x0def, 16);
  put(&b, 0, 8);
  b.bit = (b.bit + 31) & ~31u;
  b.w[start >> 5] |= ((b.bit - start) / 32 - 2) << 16;
  clear_fb(0x0001);
  setup_ccb(&c, 0, 0, 1);
  c.ccb_Flags |= CCB_PACKED;
  set_pre(&c, 6, 0, 8, 1, 2); /* PRE1 width 8: rest padded transparent */
  c.ccb_SourcePtr = (CelData *) b.w;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), 0x0001);
  CHECK_EQX(px(1, 0), 0x0001);
  CHECK_EQX(px(2, 0), 0x0abc);
  CHECK_EQX(px(4, 0), 0x0abc);
  CHECK_EQX(px(5, 0), 0x0def);
  CHECK_EQX(px(6, 0), 0x0001);
  CHECK_EQX(px(7, 0), 0x0001);
}

/* Regression for the get_bits overread fixed on 31 Oct 2026: the last pixel
   of a cel ends inside the final word of its data; nothing after that word
   may be read.  The buffer below ends exactly there (ASan reports a read past
   it; the normal build checks the pixels). */
TEST(test_last_word_not_overread)
{
  uint32_t *data = (uint32_t *) malloc(3 * sizeof(uint32_t));
  CCB c;
  int i;
  REQUIRE(data);
  for (i = 0; i < 32; i++) {
    set_plut(i, (uint16_t) (0x0100 + i));
  }
  data[0] = 0x01020304u; /* row 0: 4 x 8 bpp, stride 2 words */
  data[1] = 0xdeadbeefu; /* row 0 padding word */
  data[2] = 0x05060708u; /* row 1: its only word, the end of the buffer */
  clear_fb(0);
  setup_ccb(&c, 0, 0, 1);
  set_pre(&c, 5, 1, 4, 2, 2);
  c.ccb_Flags |= CCB_LDPLUT;
  c.ccb_PLUTPtr = plut;
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0) & 0x7fff, 0x0101);
  CHECK_EQX(px(3, 0) & 0x7fff, 0x0104);
  CHECK_EQX(px(0, 1) & 0x7fff, 0x0105);
  CHECK_EQX(px(3, 1) & 0x7fff, 0x0108);
  free(data);
}

TEST(test_skip_flag_and_list)
{
  static uint32_t d1[2] = {0x11110000u, 0}, d2[2] = {0x22220000u, 0};
  CCB a, b;
  clear_fb(0);
  setup_ccb(&a, 0, 0, 1);
  set_pre(&a, 6, 0, 1, 1, 2);
  a.ccb_SourcePtr = (CelData *) d1;
  a.ccb_Flags &= ~CCB_LAST;
  a.ccb_NextPtr = &b;
  setup_ccb(&b, 5, 0, 1);
  set_pre(&b, 6, 0, 1, 1, 2);
  b.ccb_SourcePtr = (CelData *) d2;
  DrawCels(sc.sc_BitmapItems[0], &a);
  CHECK_EQX(px(0, 0), 0x1111);
  CHECK_EQX(px(5, 0), 0x2222);
  clear_fb(0);
  a.ccb_Flags |= CCB_SKIP;
  DrawCels(sc.sc_BitmapItems[0], &a);
  CHECK_EQX(px(0, 0), 0);
  CHECK_EQX(px(5, 0), 0x2222);
}

TEST(test_clipping_off_screen)
{
  static uint32_t data[2] = {0x7fff7fffu, 0};
  CCB c;
  clear_fb(0);
  setup_ccb(&c, 319, 239, 1);
  set_pre(&c, 6, 0, 2, 1, 2);
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c); /* half off the right edge */
  CHECK_EQX(px(319, 239), 0x7fff);
  setup_ccb(&c, -1, -1, 1);
  set_pre(&c, 6, 0, 2, 1, 2);
  c.ccb_SourcePtr = (CelData *) data;
  DrawCels(sc.sc_BitmapItems[0], &c);
  CHECK_EQX(px(0, 0), 0); /* row -1 is off screen */
}

/* A fade to black leaves the display black: the next screen shown must not
   appear at full brightness before FadeFromBlack (it used to flash for one
   frame on every faded screen change) */
TEST(test_fade_state_persists_across_display)
{
  FadeToBlack(&sc, 4);
  CHECK_EQ(th_last_brightness(), 0);
  DisplayScreen(sc.sc_Screens[0], 0); /* the new picture... */
  CHECK_EQ(th_last_brightness(), 0); /* ...is still black */
  FadeFromBlack(&sc, 4);
  CHECK_EQ(th_last_brightness(), 256);
  DisplayScreen(sc.sc_Screens[1], 0);
  CHECK_EQ(th_last_brightness(), 256); /* and normal again afterwards */
}

int
main(void)
{
  SFT_BEGIN();
  if (!OpenGraphics(&sc, 2)) {
    printf("Bail out! OpenGraphics failed\n");
    return 1;
  }
  fb = sc.sc_Bitmaps[0]->bm_Buffer;
  RUN(test_uncoded16_unit_scale);
  RUN(test_scale_and_flips);
  RUN(test_pre0_bgnd_not_flip);
  RUN(test_coded_depths_use_plut);
  RUN(test_skipx);
  RUN(test_pixc_scale_and_blend);
  RUN(test_packed_without_ccbpre_has_one_preamble_word);
  RUN(test_packed_transparent_and_repeat);
  RUN(test_last_word_not_overread);
  RUN(test_skip_flag_and_list);
  RUN(test_clipping_off_screen);
  RUN(test_fade_state_persists_across_display);
  return SFT_END();
}
