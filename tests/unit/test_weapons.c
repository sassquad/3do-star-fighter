/* Weapons.c: missiles keep their full 64-bit owner.  Regression for the
 * attract-mode demo crash found on 31 Oct 2026: add_sam took its owner as
 * int32_t, big ships pass their own pointer, and war_update_attacked_friend
 * later dereferenced the truncated value. */
#include "sft.h"
#include "Ship_Struct.h"
#include "SF_ARMLink.h"
#include "Weapons.h"

extern ship_list ships;

static ship_stack *
first_active(void)
{
  ship_stack *s = (ship_stack *) ships.info.start_address;
  return (s && s->header.status == 1) ? s : NULL;
}

TEST(test_add_sam_keeps_pointer_owner)
{
  const intptr_t owner = (intptr_t) (uintptr_t) 0x7f12345678a0ull; /* > 4 GB */
  ship_stack *sam;
  armlink_initialise(MAX_SHIPS, &ships, sizeof(ship_stack), 0);
  add_sam(1 << 24, 2 << 24, 3 << 24, NULL, owner);
  sam = first_active();
  REQUIRE(sam != NULL);
  CHECK_EQX((uintptr_t) sam->who_owns_me, (uintptr_t) owner);
  CHECK_EQ(sam->x_pos, 1 << 24);
}

TEST(test_add_sam_grid_owner)
{
  ship_stack *sam;
  armlink_initialise(MAX_SHIPS, &ships, sizeof(ship_stack), 0);
  add_sam(0, 0, 0, NULL, 70 + 9 * 128); /* a ground launcher's grid reference */
  sam = first_active();
  REQUIRE(sam != NULL);
  CHECK_EQ(sam->who_owns_me, 70 + 9 * 128);
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_add_sam_keeps_pointer_owner);
  RUN(test_add_sam_grid_owner);
  return SFT_END();
}
