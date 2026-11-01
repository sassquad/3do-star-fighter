/* Maths_Stuff.c: divide, angles, distances, rotations, poly-map scanning.
 * Uses the real Cosine/Tangent tables from SF_Resources/Data. */
#include "sft.h"
#include <math.h>
#include "Maths_Stuff.h"
#include "Setup_Tables.h"
#include "sf_load.h"

#define CIRCLE (1024 * 1024) /* find_rotation units */
extern int32_t tangent_table[1025];

static int tables_loaded;

static int
load_tables(void)
{
  if (!tables_loaded) {
    sft_resources();
    tables_loaded =
        load_fileat_words(cosine_table, "Data/Cosine") != NULL &&
        load_fileat_words(tangent_table, "Data/Tangent") != NULL;
  }
  return tables_loaded;
}

TEST(test_cosine_table_shape)
{
  int a;
  REQUIRE(load_tables());
  /* the table peaks at 4095, not 4096 */
  CHECK_EQ(cosine_table[0], 4095);
  CHECK(abs(cosine_table[256]) <= 1);
  CHECK_EQ(cosine_table[512], -4095);
  CHECK(abs(sine_table[0]) <= 1);  /* sin 0 */
  CHECK_EQ(sine_table[256], 4096); /* sin 90 (entry 1024) is 4096 */
  for (a = 0; a < 1024; a++) {
    double want = 4096.0 * cos(a * 2 * M_PI / 1024);
    if (fabs(cosine_table[a] - want) > 2.0) {
      SFT_FAIL_("cosine_table[%d] = %d, expected about %.1f", a,
                cosine_table[a], want);
      return;
    }
  }
}

TEST(test_divide)
{
  CHECK_EQ(divide(7, 2), 3);
  CHECK_EQ(divide(-7, 2), -3);
  CHECK_EQ(divide(7, -2), -3);
  CHECK_EQ(divide(-7, -2), 3);
  CHECK_EQ(divide(0, 5), 0);
  CHECK_EQ(divide(1 << 30, 3), (1 << 30) / 3);
  CHECK_EQ(divide(5, 0), 0x7fffffff); /* as the original */
  CHECK_EQ(divide(-5, 0), -0x7fffffff);
  CHECK_EQ(divide(INT32_MIN, 1), INT32_MIN);
  CHECK_EQ(divide(INT32_MIN, -1), INT32_MIN); /* wraps, no trap */
}

/* find_rotation(x, y) = atan2(-x, y) as a fraction of CIRCLE: (0,+y) is 0,
   (-x,0) a quarter turn, (0,-y) half, (+x,0) three quarters. */
static long
angle_err(int32_t got, double want)
{
  long d = (long) llround(fmod(got - want + 2.5 * CIRCLE, CIRCLE)) - CIRCLE / 2;
  return labs(d);
}

TEST(test_find_rotation_axes)
{
  REQUIRE(load_tables());
  CHECK_EQ(find_rotation(0, 0), 0);
  CHECK(angle_err(find_rotation(0, 1000), 0) < 64);
  CHECK(angle_err(find_rotation(-1000, 0), CIRCLE / 4) < 64);
  CHECK(angle_err(find_rotation(0, -1000), CIRCLE / 2) < 64);
  CHECK(angle_err(find_rotation(1000, 0), 3 * CIRCLE / 4) < 64);
}

TEST(test_find_rotation_matches_atan2)
{
  int i;
  long worst = 0;
  REQUIRE(load_tables());
  for (i = 0; i < 3600; i++) {
    double th = i * 2 * M_PI / 3600;
    int32_t x = (int32_t) lround(-sin(th) * 100000),
            y = (int32_t) lround(cos(th) * 100000);
    double want = fmod(atan2(-x, y) / (2 * M_PI) * CIRCLE + CIRCLE, CIRCLE);
    int32_t got = find_rotation(x, y);
    long e = angle_err(got, want);
    if (got < 0 || got >= CIRCLE) {
      SFT_FAIL_("find_rotation(%d,%d) = %d out of range", x, y, got);
      return;
    }
    if (e > worst) {
      worst = e;
    }
  }
  /* table resolution is 1/8 degree (2048 steps per octant boundary) */
  if (worst > CIRCLE / 2048) {
    SFT_FAIL_("worst angle error %ld units (> %d)", worst, CIRCLE / 2048);
  }
}

TEST(test_find_2d_distance)
{
  int i;
  REQUIRE(load_tables());
  for (i = 0; i < 360; i += 7) {
    double th = i * M_PI / 180;
    int32_t r = 50000 + i * 1000;
    int32_t x = (int32_t) lround(r * cos(th)), y = (int32_t) lround(r * sin(th));
    int32_t d = find_2d_distance(x, y);
    if (fabs(d - hypot(x, y)) > 0.01 * r) {
      SFT_FAIL_("find_2d_distance(%d,%d) = %d, expected about %.0f", x, y, d,
                hypot(x, y));
    }
  }
  CHECK_EQ(find_2d_distance(0, 0), 0);
}

TEST(test_rotate_coords)
{
  int32_t x, y, z;
  REQUIRE(load_tables());
  x = 1000, y = 2000, z = 3000;
  rotate_coords_x(&x, &y, &z, 0); /* scale 4095/4096 */
  CHECK_EQ(x, 999);
  CHECK_EQ(y, 1999);
  CHECK_EQ(z, 3000);
  /* quarter turn about the "x" call: (x,y) -> (-y,x) */
  x = 1000, y = 0, z = 7;
  rotate_coords_x(&x, &y, &z, 256);
  CHECK(abs(x) <= 1);
  CHECK(abs(y - 1000) <= 1);
  CHECK_EQ(z, 7);
  /* "y" rotates (y,z), "z" rotates (x,z) */
  x = 5, y = 1000, z = 0;
  rotate_coords_y(&x, &y, &z, 512);
  CHECK_EQ(x, 5);
  CHECK(abs(y + 1000) <= 1);
  CHECK(abs(z) <= 1);
  x = 1000, y = 9, z = 0;
  rotate_coords_z(&x, &y, &z, 768);
  CHECK(abs(x) <= 1);
  CHECK(abs(z + 1000) <= 1);
  CHECK_EQ(y, 9);
}

/* Angles outside 0..1023 wrap (a negative ship rotation once read
   cosine_table[-1]; run under `make test-asan` to catch a regression). */
TEST(test_rotate_wraps_out_of_range_angles)
{
  static const int32_t angles[] = {-1, -256, -1023, -1024, 1024, 1300, 4095};
  size_t i;
  REQUIRE(load_tables());
  for (i = 0; i < sizeof angles / sizeof *angles; i++) {
    int32_t x1 = 1000, y1 = 500, z1 = 7, x2 = 1000, y2 = 500, z2 = 7;
    rotate_coords_x(&x1, &y1, &z1, angles[i]);
    rotate_coords_x(&x2, &y2, &z2, angles[i] & 1023);
    if (x1 != x2 || y1 != y2) {
      SFT_FAIL_("angle %d: (%d,%d), wrapped angle gives (%d,%d)", angles[i],
                x1, y1, x2, y2);
    }
  }
}

/* ---- scan_poly_map: the 32x32 window regression (PORTING_NOTES §9) ---- */
static intptr_t found[32 * 32 + 1];

static int
count_found(intptr_t want)
{
  int i, n = 0;
  for (i = 0; found[i] != -1; i++) {
    if (found[i] == want) {
      n++;
    }
    if (i > 32 * 32) {
      return -1;
    }
  }
  return n;
}

TEST(test_scan_poly_map_window)
{
  const int cx = 70, cy = 9;
  int x, y, bad = 0;
  memset(poly_map, 0, sizeof poly_map);
  poly_map[cy][cx] = 5;
  for (y = 0; y < 128; y++) {
    for (x = 0; x < 128; x++) {
      int dx = (cx - (x & ~3)) & 127, dy = (cy - y) & 127;
      int want = (dx < 32 && dy < 32) ? 1 : 0;
      int got;
      scan_poly_map(x, y, found);
      got = count_found(cx + cy * 128);
      if (got != want && bad++ < 5) {
        SFT_FAIL_("window at (%d,%d): object at (%d,%d) found %d times, "
                  "expected %d",
                  x, y, cx, cy, got, want);
      }
    }
  }
}

TEST(test_scan_poly_map_value_classes)
{
  memset(poly_map, 0, sizeof poly_map);
  poly_map[1][1] = 8;   /* <= 8: variant 1 and 2 */
  poly_map[1][2] = 9;   /* only variant 2 */
  poly_map[1][3] = 247; /* only variant 2 */
  poly_map[2][1] = 248; /* neither (collision pointer cells) */
  scan_poly_map(0, 0, found);
  CHECK_EQ(count_found(1 + 128), 1);
  CHECK_EQ(count_found(2 + 128), 0);
  CHECK_EQ(count_found(3 + 128), 0);
  CHECK_EQ(count_found(1 + 256), 0);
  scan_poly_map_2(0, 0, found);
  CHECK_EQ(count_found(1 + 128), 1);
  CHECK_EQ(count_found(2 + 128), 1);
  CHECK_EQ(count_found(3 + 128), 1);
  CHECK_EQ(count_found(1 + 256), 0);
  memset(poly_map, 0, sizeof poly_map);
  scan_poly_map(5, 5, found);
  CHECK_EQ(found[0], -1);
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_cosine_table_shape);
  RUN(test_divide);
  RUN(test_find_rotation_axes);
  RUN(test_find_rotation_matches_atan2);
  RUN(test_find_2d_distance);
  RUN(test_rotate_coords);
  RUN(test_rotate_wraps_out_of_range_angles);
  RUN(test_scan_poly_map_window);
  RUN(test_scan_poly_map_value_classes);
  return SFT_END();
}
