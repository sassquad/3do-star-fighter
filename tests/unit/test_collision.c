/* Collision.c (box test, ground height), GPTR, and the 64-bit owner fields.
 *
 * Collision.c is included directly so that its static check_collision_boxes
 * can be tested; the linker then takes these definitions instead of
 * Collision.o from libsfcore.a. */
#include "sft.h"
#include "Collision.c"
#include "Ship_Struct.h"
#include "Laser_Struct.h"
#include "Bit_Struct.h"

/* one box: type 7, min (0,0,0) max (100,200,300) */
static int32_t one_box[3 + 7] = {0, 0, 0, 7 << 24, 0, 0, 0, 100, 200, 300};

TEST(test_box_inside_outside)
{
  CHECK_EQ(check_collision_boxes(50, 50, 50, one_box), 8); /* type + 1 */
  CHECK_EQ(bonus_collision_ref, 7);
  CHECK_EQ(check_collision_boxes(150, 50, 50, one_box), 0);
  CHECK_EQ(bonus_collision_ref, 0);
  CHECK_EQ(check_collision_boxes(50, 250, 50, one_box), 0);
  CHECK_EQ(check_collision_boxes(50, 50, 350, one_box), 0);
  CHECK_EQ(check_collision_boxes(-1, 50, 50, one_box), 0);
  CHECK_EQ(check_collision_boxes(50, -1, 50, one_box), 0);
  CHECK_EQ(check_collision_boxes(50, 50, -1, one_box), 0);
}

TEST(test_box_edges)
{
  /* minimum edges count as inside, maximum edges as outside */
  CHECK_EQ(check_collision_boxes(0, 0, 0, one_box), 8);
  CHECK_EQ(check_collision_boxes(100, 50, 50, one_box), 0);
  CHECK_EQ(check_collision_boxes(50, 200, 50, one_box), 0);
  CHECK_EQ(check_collision_boxes(50, 50, 300, one_box), 0);
}

/* Reproduced assembler quirk (PORTING_NOTES §2): the compare chain stops at
   the first coordinate that is not greater than the minimum, and 'equal'
   passes - so x == min.x passes even when y or z are below their minimum. */
TEST(test_box_compare_chain_quirk)
{
  CHECK_EQ(check_collision_boxes(0, -50, -50, one_box), 8);
  CHECK_EQ(check_collision_boxes(10, 0, -50, one_box), 8);
  CHECK_EQ(check_collision_boxes(10, -1, 50, one_box), 0);
}

TEST(test_box_list_first_hit_wins)
{
  int32_t boxes[3 + 14] = {1, 0, 0,
                           /* box 0: type 2 */ 2 << 24, 0, 0, 0, 10, 10, 10,
                           /* box 1: type 5 */ 5 << 24, 0, 0, 0, 100, 100, 100};
  CHECK_EQ(check_collision_boxes(5, 5, 5, boxes), 3);
  CHECK_EQ(check_collision_boxes(50, 50, 50, boxes), 6);
  CHECK_EQ(check_collision_boxes(500, 50, 50, boxes), 0);
}

static void
fill_height(int v)
{
  memset(height_map, v, sizeof height_map);
}

TEST(test_ground_height_flat_and_clamped)
{
  fill_height(17 + 10);
  CHECK_EQ(find_ground_height(123 << 24, 45 << 24), 10 << 21);
  CHECK_EQ(find_ground_height(0x7fffffff, 0x7fffffff), 10 << 21);
  fill_height(5); /* below the 17 offset: clamped to 0 */
  CHECK_EQ(find_ground_height(10 << 24, 10 << 24), 0);
}

TEST(test_ground_height_interpolates)
{
  int32_t h0, hmid, h1;
  fill_height(17);
  ((uint8_t *) height_map)[(11 << 8) + 21] = 17 + 40; /* (x 21, y 11) */
  h0 = find_ground_height(20 << 24, 11 << 24);
  hmid = find_ground_height((20 << 24) + (1 << 23), 11 << 24);
  h1 = find_ground_height(21 << 24, 11 << 24);
  CHECK_EQ(h0, 0);
  CHECK_EQ(h1, 40 << 21);
  CHECK(abs(hmid - (20 << 21)) <= (1 << 12));
  /* wraps at the map edge: x 255 interpolates towards x 0 */
  fill_height(17);
  ((uint8_t *) height_map)[0] = 17 + 40;
  CHECK(abs(find_ground_height((int32_t) ((255u << 24) + (1u << 23)), 0) -
            (20 << 21)) <= (1 << 12));
}

/* Regression: GPTR(0) must be NULL ("no polygon group"); it used to return the
   base of graphics_data and the player's death crashed (PORTING_NOTES §9). */
TEST(test_gptr_zero_is_null)
{
  CHECK(GPTR(0) == NULL);
  CHECK((char *) GPTR(16) == (char *) graphics_data + 16);
  CHECK_EQ(GOFF(GPTR(1234)), 1234);
}

/* Regression: ship/laser/bit owner fields hold either a small integer or a
   64-bit ship pointer and must be pointer-sized (PORTING_NOTES §3, §9). */
TEST(test_owner_fields_pointer_sized)
{
  ship_stack s;
  laser_stack l;
  bit_stack b;
  CHECK_EQ(sizeof s.who_owns_me, sizeof(void *));
  CHECK_EQ(sizeof s.what_hit_me, sizeof(void *));
  CHECK_EQ(sizeof s.who_hit_me, sizeof(void *));
  CHECK_EQ(sizeof l.who_owns_me, sizeof(void *));
  CHECK_EQ(sizeof b.who_owns_me, sizeof(void *));
  /* a pointer above 4 GB survives a round trip */
  s.who_hit_me = (intptr_t) (void *) (uintptr_t) 0x7f12345678ull;
  CHECK_EQX((uintptr_t) s.who_hit_me, 0x7f12345678ull);
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_box_inside_outside);
  RUN(test_box_edges);
  RUN(test_box_compare_chain_quirk);
  RUN(test_box_list_first_hit_wins);
  RUN(test_ground_height_flat_and_clamped);
  RUN(test_ground_height_interpolates);
  RUN(test_gptr_zero_is_null);
  RUN(test_owner_fields_pointer_sized);
  return SFT_END();
}
