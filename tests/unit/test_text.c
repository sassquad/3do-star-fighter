/* sf_text.c: the game's FontLib fonts and TextLib text cels, including the
 * typewriter reveal that read past the cel data (fixed 31 Oct 2026; run under
 * `make test-asan` to catch a regression as a memory error). */
#include "sft.h"
#include "sf_compat.h"
#include "sf_video.h"

FontDescriptor *LoadFont(const char *path, int memtype);

static uint16_t *fb;
static const char *fonts[] = {"Fonts/Message", "Fonts/Menu", "Fonts/Title"};

static int
cel_width(TextCel *t)
{
  int32_t w = 0, h = 0;
  GetTextCelSize(t, &w, &h);
  return (int) w;
}

TEST(test_fonts_load)
{
  size_t i;
  sft_resources();
  for (i = 0; i < 3; i++) {
    FontDescriptor *fd = LoadFont(fonts[i], 0);
    TextCel *t;
    if (!fd) {
      SFT_FAIL_("%s did not load", fonts[i]);
      continue;
    }
    t = CreateTextCel(fd, 0, 0, 0);
    if (!t) {
      SFT_FAIL_("%s: CreateTextCel failed", fonts[i]);
      continue;
    }
    UpdateTextInCel(t, 1, "STAR FIGHTER");
    CHECK(cel_width(t) > 12);
    DeleteTextCel(t);
  }
}

TEST(test_text_widths)
{
  FontDescriptor *fd;
  TextCel *t;
  int w1, w2, w0, wsp;
  sft_resources();
  fd = LoadFont("Fonts/Menu", 0);
  REQUIRE(fd);
  t = CreateTextCel(fd, 0, 0, 0);
  REQUIRE(t);
  UpdateTextInCel(t, 1, "W");
  w1 = cel_width(t);
  UpdateTextInCel(t, 1, "WW");
  w2 = cel_width(t);
  UpdateTextInCel(t, 1, "WW WW");
  wsp = cel_width(t);
  UpdateTextInCel(t, 1, ""); /* regression: empty text must not overrun */
  w0 = cel_width(t);
  CHECK(w1 > 0);
  CHECK(w2 > w1);
  CHECK(wsp > 2 * w2);
  CHECK(w0 >= 1);
  CHECK(t->tc_size >= (size_t) 8); /* at least two words per row */
  DeleteTextCel(t);
}

static long
count_changed(int x0, int y0, int w, int h, uint16_t bg)
{
  int x, y;
  long n = 0;
  for (y = y0; y < y0 + h && y < 240; y++) {
    for (x = x0; x < x0 + w && x < 320; x++) {
      n += fb[y * 320 + x] != bg;
    }
  }
  return n;
}

/* The mission briefing and in-game messages reveal a line by raising the PRE1
   pixel count up to ccb_Width (one more than the row holds).  When the cel
   width is a multiple of 4, the extra 8-bit pixel of the last row lies past
   the cel data: strings of every length are revealed so that case is hit. */
static void
reveal_all_steps(TextCel *t, int32_t w)
{
  int32_t i;
  for (i = 0; i <= w; i++) {
    t->tc_CCB->ccb_PRE1 = (t->tc_CCB->ccb_PRE1 & ~1023u) | (uint32_t) i;
    draw_cels(0, t->tc_CCB);
  }
}

TEST(test_typewriter_reveal)
{
  static const char text[] = "MISSION 1: SCANNERS DOWN";
  FontDescriptor *fd;
  TextCel *t;
  int32_t w, h;
  int len, multiple_of_4 = 0;
  long full, partial;
  char buf[sizeof text];
  sft_resources();
  fd = LoadFont("Fonts/Message", 0);
  REQUIRE(fd);
  t = CreateTextCel(fd, 0, 0, 0);
  REQUIRE(t);
  SetTextCelColor(t, 0, MakeRGB15(31, 31, 0));
  SetTextCelCoords(t, 10, 100);
  for (len = 1; len < (int) sizeof text; len++) {
    memcpy(buf, text, (size_t) len);
    buf[len] = 0;
    UpdateTextInCel(t, 1, buf);
    GetTextCelSize(t, &w, &h);
    multiple_of_4 += (w % 4) == 0;
    memset(fb, 0, 320 * 240 * 2);
    reveal_all_steps(t, w);
  }
  CHECK(multiple_of_4 > 0); /* the overrun case was exercised */
  full = count_changed(0, 0, 320, 240, 0);
  memset(fb, 0, 320 * 240 * 2);
  t->tc_CCB->ccb_PRE1 = (t->tc_CCB->ccb_PRE1 & ~1023u) | (uint32_t) (w / 2);
  draw_cels(0, t->tc_CCB);
  partial = count_changed(0, 0, 320, 240, 0);
  CHECK(full > 50);
  CHECK(partial > 0);
  CHECK(partial < full);
  /* nothing is drawn outside the cel's rectangle */
  CHECK_EQ(count_changed(0, 0, 10, 240, 0), 0);
  CHECK_EQ(count_changed(0, 0, 320, 100, 0), 0);
  DeleteTextCel(t);
}

int
main(void)
{
  SFT_BEGIN();
  if (!video_open()) {
    printf("Bail out! video_open failed\n");
    return 1;
  }
  fb = video_buffer(0);
  RUN(test_fonts_load);
  RUN(test_text_widths);
  RUN(test_typewriter_reveal);
  return SFT_END();
}
