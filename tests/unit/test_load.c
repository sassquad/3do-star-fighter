/* sf_load.c: byte swapping, case-insensitive resource lookup, LRFORM image
 * decoding (synthetic files and every real image). */
#include "sft.h"
#include "sf_load.h"
#include <sys/stat.h>

void sf_swap_words(void *buf, size_t bytes);

static char tmproot[480];

static void
write_file(const char *rel, const void *data, size_t n)
{
  char p[1024];
  FILE *f;
  snprintf(p, sizeof p, "%s%s", tmproot, rel);
  f = fopen(p, "wb");
  if (f) {
    fwrite(data, 1, n, f);
    fclose(f);
  }
}

static void
use_tmproot(void)
{
  const char *d = sft_tempdir("load");
  char cmd[1400];
  snprintf(tmproot, sizeof tmproot, "%.470s/", d);
  snprintf(cmd, sizeof cmd, "mkdir -p '%sData' '%sImages'", tmproot, tmproot);
  if (system(cmd) != 0) {
    tmproot[0] = 0;
  }
  snprintf(sf_resource_root, sizeof sf_resource_root, "%s", tmproot);
}

TEST(test_swap_words)
{
  uint8_t b[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  uint32_t w;
  sf_swap_words(b, 10); /* only whole words are swapped */
  memcpy(&w, b, 4);
  CHECK_EQX(w, 0x01020304u);
  memcpy(&w, b + 4, 4);
  CHECK_EQX(w, 0x05060708u);
  CHECK_EQ(b[8], 9);
  CHECK_EQ(b[9], 10);
}

TEST(test_case_insensitive_lookup)
{
  uint8_t data[8] = {0, 0, 0, 1, 0, 0, 0, 2};
  uint32_t words[2] = {0, 0};
  FILE *f;
  use_tmproot();
  REQUIRE(tmproot[0]);
  write_file("Data/Cosine", data, sizeof data);
  CHECK(sf_resource_exists("Data/Cosine"));
  CHECK(sf_resource_exists("data/COSINE"));
  CHECK(sf_resource_exists("DATA//cosine"));
  CHECK(!sf_resource_exists("Data/Cosine2"));
  CHECK(!sf_resource_exists("Nothing/Cosine"));
  f = sf_fopen_resource("dAtA/cOsInE", "rb");
  CHECK(f != NULL);
  if (f) {
    fclose(f);
  }
  CHECK(sf_fopen_resource("Data/missing", "rb") == NULL);
  CHECK(load_fileat_words(words, "data/cosine") == words);
  CHECK_EQ(words[0], 1);
  CHECK_EQ(words[1], 2);
  CHECK(load_fileat(words, "Data/none") == NULL);
}

/* LRFORM: build the file from the *memory* side (memory row r, column c holds
   display pixel x = c/2 + 160*(r&1), y = 2*(r/2) + (c&1)) and check the
   decoder puts every pixel back. */
static uint16_t
code_for(int x, int y)
{
  return (uint16_t) ((x * 97 + y * 331) & 0x7fff);
}

static uint8_t lr[320 * 240 * 2 + 64];
static uint16_t img[320 * 240];

static void
build_lrform(uint8_t *out)
{
  int r, c;
  for (r = 0; r < 240; r++) {
    for (c = 0; c < 320; c++) {
      int x = c / 2 + 160 * (r & 1), y = 2 * (r / 2) + (c & 1);
      uint16_t v = code_for(x, y);
      out[(r * 320 + c) * 2] = (uint8_t) (v >> 8);
      out[(r * 320 + c) * 2 + 1] = (uint8_t) v;
    }
  }
}

static int
check_img(void)
{
  int x, y, bad = 0;
  for (y = 0; y < 240; y++) {
    for (x = 0; x < 320; x++) {
      if (img[y * 320 + x] != code_for(x, y) && bad++ < 3) {
        SFT_FAIL_("pixel (%d,%d) = 0x%04x, expected 0x%04x", x, y,
                  img[y * 320 + x], code_for(x, y));
      }
    }
  }
  return bad == 0;
}

TEST(test_lrform_bare)
{
  use_tmproot();
  build_lrform(lr);
  write_file("Images/Bare", lr, 320 * 240 * 2);
  memset(img, 0, sizeof img);
  REQUIRE(load_image_raw(img, "Images/Bare") == img);
  CHECK(check_img());
}

TEST(test_lrform_chunked)
{
  static uint8_t f[16 + 8 + 320 * 240 * 2];
  /* IMAG chunk of 16 bytes, then PDAT */
  memset(f, 0, sizeof f);
  memcpy(f, "IMAG", 4);
  f[7] = 16;
  memcpy(f + 16, "PDAT", 4);
  build_lrform(f + 24);
  use_tmproot();
  write_file("Images/Chunked", f, sizeof f);
  memset(img, 0, sizeof img);
  REQUIRE(load_image_raw(img, "Images/Chunked") == img);
  CHECK(check_img());
  /* too short, or no PDAT: refused */
  write_file("Images/Short", lr, 1000);
  CHECK(load_image_raw(img, "Images/Short") == NULL);
  memcpy(f + 16, "XXXX", 4);
  write_file("Images/NoPdat", f, sizeof f);
  CHECK(load_image_raw(img, "Images/NoPdat") == NULL);
}

TEST(test_real_images_load)
{
  static const char *names[] = {"Fednet", "Mission_Select", "Menu0", "Menu1",
                                "Menu2",  "Menu3",          "Menu4", "Menu5",
                                "Menu6",  "Menu7"};
  size_t i;
  sft_resources();
  for (i = 0; i < sizeof names / sizeof *names; i++) {
    char rel[64];
    long nonzero = 0;
    int k;
    snprintf(rel, sizeof rel, "Images/%s", names[i]);
    memset(img, 0, sizeof img);
    if (load_image_raw(img, rel) != img) {
      SFT_FAIL_("%s did not load", rel);
      continue;
    }
    for (k = 0; k < 320 * 240; k++) {
      nonzero += img[k] != 0;
    }
    if (nonzero < 1000) { /* logos on black are legitimately dark */
      SFT_FAIL_("%s is blank (%ld pixels set)", rel, nonzero);
    }
  }
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_swap_words);
  RUN(test_case_insensitive_lookup);
  RUN(test_lrform_bare);
  RUN(test_lrform_chunked);
  RUN(test_real_images_load);
  return SFT_END();
}
