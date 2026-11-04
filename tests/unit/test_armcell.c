/* SF_ARMCell.c: 16x16 ground cels built from 4x4 tiles (with cache) and the
 * 6-bit landscape maps, and that the mid-ring and far-ring ground cels put a
 * map feature in the same place (the level-of-detail seam). */
#include "sft.h"
#include "sf_compat.h"
#include "sf_video.h"
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
static uint32_t m512[512 * 96], m128[128 * 24], m32[32 * 6];

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

/* ---- the same ground through two levels of detail ----
   Mid ring: arm_add4cel4, a 16x16 cel covering 4x4 map units.  Far ring:
   arm_addcelfrom512map, a 16x16 cel cut from the 512 map covering 8x8 units.
   Both are drawn at 4 screen pixels per map unit with corners in the order
   Plot_Land uses (P0 = (x, y), P1 = (x+n, y), P3 = (x, y+n)), so map row Y
   should land on screen rows 4*(Y - y)..+3 in both.  Regression: PRE0 bit
   30 (BGND) was taken for a vertical flip, mirroring every far cell. */
static uint16_t *fb;

#define ORIGIN_X 100 /* screen position of the cell's (x, y) corner */
#define ORIGIN_Y 40

static void
corners(int units)
{
  int side = units * 4;
  cq.x_pos0 = ORIGIN_X - 160;
  cq.y_pos0 = ORIGIN_Y - 120;
  cq.x_pos1 = cq.x_pos0 + side;
  cq.y_pos1 = cq.y_pos0;
  cq.x_pos2 = cq.x_pos0 + side;
  cq.y_pos2 = cq.y_pos0 + side;
  cq.x_pos3 = cq.x_pos0;
  cq.y_pos3 = cq.y_pos0 + side;
}

/* ground of tile 0 with a one-unit-wide road of tile 1 along map row (or
   column) 'at' */
static void
road_world(int at, int along_x)
{
  int x, y, i;
  setup();
  for (i = 0; i < 4; i++) {
    uint32_t grass = 5, road = 27; /* 6-bit pixel values */
    tiles[0 * 4 + i] = (grass << 26) | (grass << 20) | (grass << 14) | (grass << 8);
    tiles[1 * 4 + i] = (road << 26) | (road << 20) | (road << 14) | (road << 8);
  }
  for (y = 0; y < 256; y++) {
    for (x = 0; x < 256; x++) {
      map[x + (y << 8)] = (uint8_t) ((along_x ? y : x) == at);
    }
  }
  for (i = 0; i < 16; i++) { /* PLUT: entry n = a distinct visible colour */
    pal[i] = ((uint32_t) (0x0421 * (2 * i + 1)) << 16) | (0x0421u * (2 * i + 2));
  }
  cq.cel_map512 = (char *) m512;
  cq.cel_map128 = (char *) m128;
  cq.cel_map32 = (char *) m32;
  arm_generatemaps(&cq, map);
  arm_celinitialisecreation(creation, 257); /* headers for all 256 slots */
  cq.shade = 8;
}

static uint16_t grass; /* colour of the ground without the road */

/* screen rows (or columns) 0..side-1 of the cell that differ from the ground */
static unsigned
road_lines(int side, int along_x)
{
  unsigned lines = 0;
  int k;
  for (k = 0; k < side; k++) {
    int x = ORIGIN_X + (along_x ? side / 2 : k), y = ORIGIN_Y + (along_x ? k : side / 2);
    if (fb[y * 320 + x] != grass) {
      lines |= 1u << k;
    }
  }
  return lines;
}

static unsigned draw_far(int x, int y, int along_x);

/* the ground colour as drawn by a level (a world without the road) */
static void
learn_grass(int far)
{
  road_world(-100, 1);
  memset(fb, 0, 320 * 240 * 2);
  cq.temp_cels = 0;
  corners(far ? 8 : 4);
  if (far) {
    arm_addcelfrom512map(&cq, 8, 16, 0);
  } else {
    arm_add4cel4(&cq, map, 8, 16);
  }
  ccbs[0].ccb_Flags |= CCB_LAST;
  draw_cels(0, &ccbs[0]);
  grass = fb[(ORIGIN_Y + 2) * 320 + ORIGIN_X + 2];
}

static unsigned
draw_mid(int x, int y, int along_x)
{
  memset(fb, 0, 320 * 240 * 2);
  cq.temp_cels = 0;
  corners(4);
  arm_add4cel4(&cq, map, x, y);
  ccbs[0].ccb_Flags |= CCB_LAST;
  draw_cels(0, &ccbs[0]);
  return road_lines(16, along_x);
}

static unsigned
draw_far(int x, int y, int along_x)
{
  memset(fb, 0, 320 * 240 * 2);
  cq.temp_cels = 0;
  corners(8);
  arm_addcelfrom512map(&cq, x, y, 0);
  ccbs[0].ccb_Flags |= CCB_LAST;
  draw_cels(0, &ccbs[0]);
  return road_lines(32, along_x);
}

TEST(test_lod_rings_agree_on_map_rows)
{
  int k;
  for (k = 0; k < 4; k++) { /* road on each row of the mid cell at y = 16 */
    unsigned want = 0xFu << (4 * k), mid, far;
    learn_grass(0);
    road_world(16 + k, 1);
    mid = draw_mid(8, 16, 1);
    learn_grass(1);
    road_world(16 + k, 1);
    far = draw_far(8, 16, 1) & 0xFFFF; /* the far cell's first 4 units */
    if (mid != want || far != want) {
      SFT_FAIL_("road on map row %d: mid rows 0x%04x, far rows 0x%04x, expected 0x%04x",
                16 + k, mid, far, want);
    }
  }
  learn_grass(1);
  road_world(16 + 6, 1); /* and in the far cell's second half */
  CHECK_EQX(draw_far(8, 16, 1), 0xFu << 24);
}

TEST(test_lod_rings_agree_on_map_columns)
{
  int k;
  for (k = 0; k < 4; k++) {
    unsigned want = 0xFu << (4 * k), mid, far;
    learn_grass(0);
    road_world(8 + k, 0);
    mid = draw_mid(8, 16, 0);
    learn_grass(1);
    road_world(8 + k, 0);
    far = draw_far(8, 16, 0) & 0xFFFF;
    if (mid != want || far != want) {
      SFT_FAIL_("road on map column %d: mid 0x%04x, far 0x%04x, expected 0x%04x",
                8 + k, mid, far, want);
    }
  }
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
  RUN(test_add4cel4_uses_tile_n);
  RUN(test_add4cel4_cache_hit_reuses_cel);
  RUN(test_generatemaps_bit_packing);
  RUN(test_lod_rings_agree_on_map_rows);
  RUN(test_lod_rings_agree_on_map_columns);
  return SFT_END();
}
