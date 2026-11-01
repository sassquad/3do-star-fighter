/* SF_Bonus.c: bonus_downgrade_ship (damage to the player's ship).  Regression
 * for the negative shift found by UBSan: damage ratings above 3 shifted by a
 * negative amount; it now follows ARM semantics (shift >= 32 gives 0), so
 * the weapon stats picked by choices 0-6 are left alone.  Run under
 * `make test-asan` to catch undefined behaviour. */
#include "sft.h"
#include "SF_Mission.h"
#include "Ship_Struct.h"
#include "SF_ARMUtils.h"
#include "SF_Bonus.h"

extern performance_data player_performance;
extern ship_stack *players_ship;

static ship_stack dummy; /* shields 0: no "damaged" message is shown */

static void
reset_stats(void)
{
  int i;
  player_performance.engine = 50;
  player_performance.control = 50;
  player_performance.shields = 50;
  for (i = 0; i < 13; i++) {
    player_performance.weapons[i] = 40;
  }
}

static int
check_ranges(void)
{
  int i;
  if (player_performance.engine < 1 || player_performance.engine > 50 ||
      player_performance.control < 1 || player_performance.control > 50 ||
      player_performance.shields < 1 || player_performance.shields > 50) {
    return 0;
  }
  for (i = 0; i < 13; i++) {
    if (player_performance.weapons[i] < 0 ||
        player_performance.weapons[i] > 40) {
      return 0;
    }
  }
  return 1;
}

TEST(test_downgrade_all_ratings_stay_in_range)
{
  int rating, k;
  players_ship = &dummy;
  arm_randominit();
  for (rating = 0; rating <= 7; rating++) {
    reset_stats();
    for (k = 0; k < 300; k++) {
      bonus_downgrade_ship(rating);
      if (!check_ranges()) {
        SFT_FAIL_("rating %d, call %d: a stat left its range", rating, k);
        return;
      }
    }
  }
}

TEST(test_high_rating_spares_weapons)
{
  int k, i, weapons_hit = 0, others_hit = 0;
  players_ship = &dummy;
  arm_randominit();
  reset_stats();
  for (k = 0; k < 300; k++) {
    bonus_downgrade_ship(7); /* shift 3-7 = -4: ARM gives 0, so takeoff 0 */
  }
  for (i = 0; i < 13; i++) {
    weapons_hit += player_performance.weapons[i] != 40;
  }
  others_hit = (player_performance.engine != 50) +
               (player_performance.control != 50) +
               (player_performance.shields != 50);
  /* choices 0-6 (weapons, via the shift) take nothing; choice 7 is also a
     weapon but loses rating/2 like the engine/control/shields choices 8-10 */
  CHECK(weapons_hit <= 1);
  CHECK(others_hit > 0);
}

TEST(test_full_rating_damages_weapons)
{
  int k, i, weapons_hit = 0;
  players_ship = &dummy;
  arm_randominit();
  reset_stats();
  for (k = 0; k < 300; k++) {
    bonus_downgrade_ship(3); /* shift 0: up to the whole rating */
  }
  for (i = 0; i < 13; i++) {
    weapons_hit += player_performance.weapons[i] != 40;
  }
  CHECK(weapons_hit > 0);
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_downgrade_all_ratings_stay_in_range);
  RUN(test_high_rating_spares_weapons);
  RUN(test_full_rating_damages_weapons);
  return SFT_END();
}
