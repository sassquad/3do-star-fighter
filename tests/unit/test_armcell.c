/* SF_ARMCell.c: 16x16 ground cels built from 4x4 tiles (with cache) and the
 * 6-bit landscape maps. */
#include "sft.h"
#include "sf_compat.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"

static cel_celdata cq;
static CCB ccbs[8];
static CCB *plot[8];
static uint8_t lookup[256 * 64];
static uint8_t creation[256 * 264];
static uint8_t freeslots[256];
static uint32_t tiles[256 * 4];
static uint8_t map[256 * 256];
static uint32_t pal[16];

static void
setup(void)
{
  int i;
  memset(&cq, 0, sizeof cq);
  memset(lookup, 0, sizeof lookup);
  memset(creation, 0, sizeof creation);
  for (i = 0; i < 8; i++) {
    plot[i] = &ccbs[i];
  }
  for (i = 0; i < 256; i++) {
    freeslots[i] = (uint8_t) i;
  }
  /* tile n, word k: n in the top byte, k in the next, so every word read can
     be traced back to its tile */
  for (i = 0; i < 256 * 4; i++) {
    tiles[i] = ((uint32_t) (i / 4) << 24) | ((uint32_t) (i % 4) << 16) |
               0x00000100u * (uint32_t) (i % 4);
  }
  cq.plotlist = plot;
  cq.cels4x4 = (char *) tiles;
  cq.cel_creation = (char *) creation;
  cq.cache_lookup = (char *) lookup;
  cq.cache_free = (char *) freeslots;
  cq.cache_freecount = 256;
  cq.cel_codedpalette = (char *) pal;
}

/* Regression (PORTING_NOTES §9): tile n is at word n*4 of the .4 file; the
   port once skipped a non-existent 16-byte header and used tile n+1. */
TEST(test_add4cel4_uses_tile_n)
{
  const int x = 8, y = 12;
  uint32_t *src;
  int g, col;
  setup();
  memset(map, 0, sizeof map);
  for (g = 0; g < 4; g++) {
    for (col = 0; col < 4; col++) {
      map[(x + col) + ((y + g) << 8)] = (uint8_t) (10 + g * 4 + col);
    }
  }
  arm_add4cel4(&cq, map, x, y);
  REQUIRE(cq.temp_cels == 1);
  src = (uint32_t *) ccbs[0].ccb_SourcePtr;
  REQUIRE(src != NULL);
  /* tile group g occupies words 2 + g*12 ...; its first word is
     column 0's last tile word (top byte = tile number) */
  for (g = 0; g < 4; g++) {
    uint32_t w = src[2 + g * 12];
    int tile = (int) (w >> 24);
    if (tile != 10 + g * 4) {
      SFT_FAIL_("tile row %d starts with tile %d, expected %d", g, tile,
                10 + g * 4);
    }
    CHECK_EQ((w >> 16) & 0xff, 3); /* rows are read last word first */
    /* the low byte holds the next column's tile number (b >> 24) */
    CHECK_EQ(w & 0xff, 10 + g * 4 + 1);
  }
}

TEST(test_add4cel4_cache_hit_reuses_cel)
{
  uint32_t *first;
  int before;
  setup();
  memset(map, 3, sizeof map);
  arm_add4cel4(&cq, map, 4, 4);
  first = (uint32_t *) ccbs[0].ccb_SourcePtr;
  before = cq.cache_freecount;
  map[4 + (4 << 8)] = 99; /* changing the map now has no effect: cached */
  arm_add4cel4(&cq, map, 4, 4);
  CHECK_EQ(cq.temp_cels, 2);
  CHECK(ccbs[1].ccb_SourcePtr == (CelData *) first);
  CHECK_EQ(cq.cache_freecount, before);
  arm_add4cel4(&cq, map, 16, 4); /* another cell: a new slot */
  CHECK_EQ(cq.cache_freecount, before - 1);
  CHECK(ccbs[2].ccb_SourcePtr != (CelData *) first);
}

/* 6-bit maps: unpack independently and compare with the tiles */
static uint32_t m512[512 * 96], m128[128 * 24], m32[32 * 6];

static uint32_t
get6(const uint32_t *row, int idx)
{
  int bit = idx * 6, w = bit >> 5, off = bit & 31;
  uint64_t win = ((uint64_t) row[w] << 32) | (off > 26 ? row[w + 1] : 0);
  return (uint32_t) (win >> (64 - 6 - off)) & 63;
}

TEST(test_generatemaps_bit_packing)
{
  int x, y, bad = 0;
  uint32_t s = 99;
  setup();
  for (x = 0; x < 256 * 4; x++) { /* random tile words */
    s = s * 1664525u + 1013904223u;
    tiles[x] = s;
  }
  for (x = 0; x < 256 * 256; x++) {
    s = s * 1664525u + 1013904223u;
    map[x] = (uint8_t) (s >> 24);
  }
  cq.cel_map512 = (char *) m512;
  cq.cel_map128 = (char *) m128;
  cq.cel_map32 = (char *) m32;
  arm_generatemaps(&cq, map);
  for (y = 0; y < 512 && bad < 3; y++) {
    for (x = 0; x < 256 && bad < 3; x++) {
      const uint32_t *t = tiles + map[x + ((y >> 1) << 8)] * 4;
      uint32_t w = (y & 1) ? t[0] : t[2];
      if (get6(m512 + y * 96, 2 * x) != w >> 26 ||
          get6(m512 + y * 96, 2 * x + 1) != ((w >> 14) & 63)) {
        SFT_FAIL_("512 map mismatch at (%d,%d)", x, y);
        bad++;
      }
    }
  }
  for (y = 0; y < 128; y++) {
    for (x = 0; x < 128; x++) {
      if (get6(m128 + y * 24, x) !=
          tiles[map[(x << 1) + (y << 9)] * 4] >> 26) {
        SFT_FAIL_("128 map mismatch at (%d,%d)", x, y);
        return;
      }
    }
  }
  for (y = 0; y < 32; y++) {
    for (x = 0; x < 32; x++) {
      if (get6(m32 + y * 6, x) != tiles[map[(x << 3) + (y << 11)] * 4] >> 26) {
        SFT_FAIL_("32 map mismatch at (%d,%d)", x, y);
        return;
      }
    }
  }
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_add4cel4_uses_tile_n);
  RUN(test_add4cel4_cache_hit_reuses_cel);
  RUN(test_generatemaps_bit_packing);
  return SFT_END();
}
