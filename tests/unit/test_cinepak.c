/* sf_cinepak.c: decode every frame of every video, a checksum of the Krisalis
 * logo video, and robustness against truncated or corrupted packets (run under
 * `make test-asan` to catch out-of-bounds reads). */
#include "sft.h"
#include "sf_cinepak.h"
#include "sf_load.h"

/* Checksum of all decoded Krisalis frames, recorded from the decoder whose
   output was checked visually (PORTING_NOTES §8).  If the decoder is changed
   on purpose, re-check the video by eye and update these. */
#define KRISALIS_FRAMES 143
#define KRISALIS_HASH 0x0d363c41dd957746ull

typedef struct {
  const uint8_t *data;
  size_t len;
} pkt;

static uint8_t *file;
static size_t file_len;
static pkt *pkts;
static int npkts;

static uint32_t
be32(const uint8_t *p)
{
  return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
         ((uint32_t) p[2] << 8) | p[3];
}

/* Same chunk walk as SF_Video.c: FILM/FRME chunks carry one Cinepak packet
   at offset 28. */
static int
index_video(const char *name)
{
  char rel[128];
  FILE *f;
  long n;
  size_t p;
  free(file);
  free(pkts);
  file = NULL;
  pkts = NULL;
  npkts = 0;
  snprintf(rel, sizeof rel, "Video/%s", name);
  f = sf_fopen_resource(rel, "rb");
  if (!f) {
    return 0;
  }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  file = (uint8_t *) malloc((size_t) n);
  if (!file || fread(file, 1, (size_t) n, f) != (size_t) n) {
    fclose(f);
    return 0;
  }
  fclose(f);
  file_len = (size_t) n;
  pkts = (pkt *) malloc(sizeof *pkts * (file_len / 32 + 1));
  for (p = 0; p + 16 <= file_len;) {
    uint32_t sz = be32(file + p + 4);
    if (sz < 16 || p + sz > file_len) {
      break;
    }
    if (memcmp(file + p, "FILM", 4) == 0 && sz > 32 &&
        memcmp(file + p + 16, "FRME", 4) == 0) {
      pkts[npkts].data = file + p + 28;
      pkts[npkts].len = sz - 28;
      npkts++;
    }
    p += sz;
  }
  return npkts > 0;
}

static int
dims(int *w, int *h)
{
  *w = (pkts[0].data[4] << 8 | pkts[0].data[5]) & ~3;
  *h = (pkts[0].data[6] << 8 | pkts[0].data[7]) & ~3;
  return *w >= 4 && *h >= 4 && *w <= 320 && *h <= 240;
}

TEST(test_krisalis_checksum)
{
  cinepak_ctx *c;
  int w, h, i, errors = 0;
  uint64_t hash = 1469598103934665603ull;
  sft_resources();
  REQUIRE(index_video("Krisalis"));
  REQUIRE(dims(&w, &h));
  c = cinepak_create(w, h);
  REQUIRE(c);
  for (i = 0; i < npkts; i++) {
    const uint16_t *px;
    int k;
    if (cinepak_decode(c, pkts[i].data, pkts[i].len) != 0) {
      errors++;
    }
    px = cinepak_pixels(c);
    for (k = 0; k < w * h; k++) {
      hash = (hash ^ px[k]) * 1099511628211ull;
    }
  }
  cinepak_destroy(c);
  CHECK_EQ(errors, 0);
  if (getenv("SFT_VERBOSE")) {
    printf("# Krisalis: %dx%d, %d frames, hash 0x%016llx\n", w, h, npkts,
           (unsigned long long) hash);
  }
  CHECK_EQ(npkts, KRISALIS_FRAMES);
  CHECK_EQX(hash, KRISALIS_HASH);
}

TEST(test_all_videos_decode)
{
  static const char *names[] = {"Krisalis", "StarLogo", "Intro",   "into1",
                                "into2",    "into3",    "Victory0", "Victory1",
                                "Failure",  "Outro"};
  size_t v;
  sft_resources();
  for (v = 0; v < sizeof names / sizeof *names; v++) {
    cinepak_ctx *c;
    int w, h, i, errors = 0;
    if (!index_video(names[v])) {
      SFT_FAIL_("%s: no video frames", names[v]);
      continue;
    }
    if (!dims(&w, &h)) {
      SFT_FAIL_("%s: bad size %dx%d", names[v], w, h);
      continue;
    }
    c = cinepak_create(w, h);
    for (i = 0; i < npkts; i++) {
      errors += cinepak_decode(c, pkts[i].data, pkts[i].len) != 0;
    }
    cinepak_destroy(c);
    if (getenv("SFT_VERBOSE")) {
      printf("# %-9s %dx%d %5d frames, %d errors\n", names[v], w, h, npkts,
             errors);
    }
    if (errors) {
      SFT_FAIL_("%s: %d of %d frames failed to decode", names[v], errors,
                npkts);
    }
  }
}

/* Truncated and corrupted packets must be rejected or decoded without reading
   outside the packet (the copies are heap blocks of exactly the packet size,
   so ASan sees any overread). */
TEST(test_corrupt_packets)
{
  cinepak_ctx *c;
  int w, h, i, trial;
  uint32_t s = 2024;
  sft_resources();
  REQUIRE(index_video("Krisalis"));
  REQUIRE(dims(&w, &h));
  c = cinepak_create(w, h);
  for (trial = 0; trial < 3000; trial++) {
    const pkt *src = &pkts[trial % npkts];
    size_t len = src->len;
    uint8_t *copy;
    s = s * 1664525u + 1013904223u;
    if (trial % 3 == 0) {
      len = (s >> 8) % (len + 1); /* truncated */
    }
    copy = (uint8_t *) malloc(len ? len : 1);
    memcpy(copy, src->data, len);
    if (trial % 3 != 0 && len) {
      int flips = 1 + (int) ((s >> 4) % 8);
      for (i = 0; i < flips; i++) {
        s = s * 1664525u + 1013904223u;
        copy[(s >> 8) % len] ^= (uint8_t) (1u << ((s >> 3) & 7));
      }
    }
    (void) cinepak_decode(c, copy, len);
    free(copy);
  }
  cinepak_destroy(c);
  CHECK(1); /* reaching here without a crash is the test */
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_krisalis_checksum);
  RUN(test_all_videos_decode);
  RUN(test_corrupt_packets);
  return SFT_END();
}
