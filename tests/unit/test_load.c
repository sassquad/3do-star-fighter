/* sf_load.c: byte swapping, case-insensitive resource lookup, PNG backdrop
 * loading (a synthetic image and every real one). */
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

/* PNG backdrops: a synthetic 320x240 image whose every pixel encodes its own
   position, written as an uncompressed PNG (stored deflate blocks), must come
   back exactly; the 5-bit channels are stored as c<<3 | c>>2. */
static uint16_t
code_for(int x, int y)
{
  return (uint16_t) ((x * 97 + y * 331) & 0x7fff);
}

static uint16_t img[320 * 240];

static uint32_t
crc32_(const uint8_t *p, size_t n, uint32_t c)
{
  size_t i;
  int k;
  c = ~c;
  for (i = 0; i < n; i++) {
    c ^= p[i];
    for (k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
  }
  return ~c;
}

static size_t
put_chunk(uint8_t *out, const char *tag, const uint8_t *data, size_t n)
{
  uint32_t crc;
  out[0] = (uint8_t) (n >> 24);
  out[1] = (uint8_t) (n >> 16);
  out[2] = (uint8_t) (n >> 8);
  out[3] = (uint8_t) n;
  memcpy(out + 4, tag, 4);
  if (n) {
    memcpy(out + 8, data, n);
  }
  crc = crc32_(out + 4, n + 4, 0);
  out[8 + n] = (uint8_t) (crc >> 24);
  out[9 + n] = (uint8_t) (crc >> 16);
  out[10 + n] = (uint8_t) (crc >> 8);
  out[11 + n] = (uint8_t) crc;
  return n + 12;
}

/* 8-bit RGB PNG of w x h from 'rgb', zlib with stored blocks; returns size */
static size_t
build_png(uint8_t *out, const uint8_t *rgb, int w, int h)
{
  static uint8_t raw[(320 * 3 + 1) * 240], z[sizeof raw + 2048];
  uint8_t ihdr[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 2, 0, 0, 0};
  size_t nraw = 0, nz = 0, pos, o = 0;
  uint32_t a = 1, b = 0;
  int y;
  for (y = 0; y < h; y++) {
    raw[nraw++] = 0;
    memcpy(raw + nraw, rgb + (size_t) y * w * 3, (size_t) w * 3);
    nraw += (size_t) w * 3;
  }
  z[nz++] = 0x78;
  z[nz++] = 0x01;
  for (pos = 0; pos < nraw;) {
    size_t n = nraw - pos > 65535 ? 65535 : nraw - pos;
    z[nz++] = (uint8_t) (pos + n == nraw);
    z[nz++] = (uint8_t) n;
    z[nz++] = (uint8_t) (n >> 8);
    z[nz++] = (uint8_t) ~n;
    z[nz++] = (uint8_t) (~n >> 8);
    memcpy(z + nz, raw + pos, n);
    nz += n;
    pos += n;
  }
  for (pos = 0; pos < nraw; pos++) {
    a = (a + raw[pos]) % 65521;
    b = (b + a) % 65521;
  }
  z[nz++] = (uint8_t) (b >> 8);
  z[nz++] = (uint8_t) b;
  z[nz++] = (uint8_t) (a >> 8);
  z[nz++] = (uint8_t) a;
  memcpy(out, "\x89PNG\r\n\x1a\n", 8);
  o = 8;
  ihdr[2] = (uint8_t) (w >> 8);
  ihdr[3] = (uint8_t) w;
  ihdr[6] = (uint8_t) (h >> 8);
  ihdr[7] = (uint8_t) h;
  o += put_chunk(out + o, "IHDR", ihdr, 13);
  o += put_chunk(out + o, "IDAT", z, nz);
  o += put_chunk(out + o, "IEND", NULL, 0);
  return o;
}

static uint8_t rgb[320 * 240 * 3], png[sizeof rgb + 4096];

static int
c8(int c)
{
  return (c << 3) | (c >> 2);
}

TEST(test_png_backdrop_exact)
{
  int x, y, bad = 0;
  size_t n;
  for (y = 0; y < 240; y++) {
    for (x = 0; x < 320; x++) {
      uint16_t v = code_for(x, y);
      uint8_t *p = rgb + (y * 320 + x) * 3;
      p[0] = (uint8_t) c8((v >> 10) & 31);
      p[1] = (uint8_t) c8((v >> 5) & 31);
      p[2] = (uint8_t) c8(v & 31);
    }
  }
  use_tmproot();
  n = build_png(png, rgb, 320, 240);
  write_file("Images/Synthetic.png", png, n);
  memset(img, 0, sizeof img);
  REQUIRE(load_image_png(img, "Images/Synthetic") == img);
  for (y = 0; y < 240; y++) {
    for (x = 0; x < 320; x++) {
      if (img[y * 320 + x] != code_for(x, y) && bad++ < 3) {
        SFT_FAIL_("pixel (%d,%d) = 0x%04x, expected 0x%04x", x, y,
                  img[y * 320 + x], code_for(x, y));
      }
    }
  }
  /* the wrong size, a damaged file or no file at all: refused */
  n = build_png(png, rgb, 16, 16);
  write_file("Images/Small.png", png, n);
  CHECK(load_image_png(img, "Images/Small") == NULL);
  n = build_png(png, rgb, 320, 240);
  write_file("Images/Cut.png", png, n / 2);
  CHECK(load_image_png(img, "Images/Cut") == NULL);
  CHECK(load_image_png(img, "Images/Missing") == NULL);
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
    if (load_image_png(img, rel) != img) {
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
  RUN(test_png_backdrop_exact);
  RUN(test_real_images_load);
  return SFT_END();
}
