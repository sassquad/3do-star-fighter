/* SF_Access.c parsers against every real data file: missions (E, M, D),
 * planet .info files and the default configuration. */
#include "sft.h"
#include <strings.h>
#include "SF_Access.h"
#include "SF_Mission.h"
#include "SF_Parameters.h"
#include "sf_load.h"

/* defined in SF_Access.c, not declared in a header */
void *load_mission_file(mission_data *m, const char *fmt, ...);
void *load_planet_file(planet_data *p, const char *fmt, ...);
void *load_config_file(game_configuration *c, const char *fmt, ...);

static mission_data m;

static const char *known_planets[] = {"Earth",   "Earth2", "Space",
                                      "Chemical", "Death",  "Warrior",
                                      "Ice",     "Plastic"};

static int
known_planet(const char *p)
{
  size_t i;
  for (i = 0; i < sizeof known_planets / sizeof *known_planets; i++) {
    if (strcasecmp(p, known_planets[i]) == 0) { /* T/14 says "ICE" */
      return 1;
    }
  }
  return 0;
}

static void
check_mission(char level, int n)
{
  int i;
  memset(&m, 0x5a, sizeof m);
  if (load_mission_file(&m, "Missions/%c/Miss_%d", level, n) != &m) {
    SFT_FAIL_("mission %c/%d did not load", level, n);
    return;
  }
  if (memchr(m.planettype, 0, sizeof m.planettype) == NULL ||
      !known_planet(m.planettype)) {
    SFT_FAIL_("mission %c/%d: unknown planet type '%.16s'", level, n,
              m.planettype);
  }
  if (memchr(m.skyfile, 0, sizeof m.skyfile) == NULL || !m.skyfile[0]) {
    SFT_FAIL_("mission %c/%d: bad sky file name", level, n);
  }
  if (m.groundcount < 0 || m.groundcount > 128) {
    SFT_FAIL_("mission %c/%d: groundcount %d", level, n, m.groundcount);
  }
  if (m.flightpath_count < 0 || m.flightpath_count > 8) {
    SFT_FAIL_("mission %c/%d: flightpath_count %d", level, n,
              m.flightpath_count);
  }
  for (i = 0; i < 8; i++) {
    if (m.flight_paths[i].flight_points < 0 ||
        m.flight_paths[i].flight_points > 64) {
      SFT_FAIL_("mission %c/%d: path %d has %d points", level, n, i,
                m.flight_paths[i].flight_points);
    }
  }
  if (m.special_ships < 0 || m.special_ships > 32) {
    SFT_FAIL_("mission %c/%d: special_ships %d", level, n, m.special_ships);
  }
  for (i = 0; i < 32; i++) {
    if (m.ship_special[i].ship_address || m.ship_special[i].command_address) {
      SFT_FAIL_("mission %c/%d: runtime pointers not cleared", level, n);
      break;
    }
  }
  if (memchr(m.title, 0, sizeof m.title) == NULL) {
    SFT_FAIL_("mission %c/%d: title not terminated", level, n);
  }
  if (getenv("SFT_VERBOSE")) {
    printf("# %c/%-2d %-9s sky=%-10s ground=%3d paths=%d specials=%2d "
           "docked=%d title='%s'\n",
           level, n, m.planettype, m.skyfile, m.groundcount,
           m.flightpath_count, m.special_ships, m.starting_docked, m.title);
  }
}

TEST(test_all_missions_parse)
{
  int n;
  sft_resources();
  for (n = 1; n <= 15; n++) { /* four pyramids: T(raining) E M H */
    check_mission('T', n);
    check_mission('E', n);
    check_mission('M', n);
    check_mission('H', n);
  }
  check_mission('D', 1); /* demo (attract mode) missions */
  check_mission('D', 2);
}

TEST(test_mission_files_have_exact_size)
{
  /* the parser reads exactly 4924 bytes; a different size means a different
     format */
  static const char lv[] = {'T', 'E', 'M', 'H'};
  int n, l;
  char rel[64];
  sft_resources();
  for (l = 0; l < 4; l++) {
    for (n = 1; n <= 15; n++) {
      FILE *f;
      long sz;
      snprintf(rel, sizeof rel, "Missions/%c/MISS_%d", lv[l], n);
      f = sf_fopen_resource(rel, "rb");
      if (!f) {
        SFT_FAIL_("%s missing", rel);
        continue;
      }
      fseek(f, 0, SEEK_END);
      sz = ftell(f);
      fclose(f);
      if (sz != 4924) {
        SFT_FAIL_("%s is %ld bytes", rel, sz);
      }
    }
  }
}

TEST(test_missing_mission_fails)
{
  sft_resources();
  CHECK(load_mission_file(&m, "Missions/Z/Miss_%d", 1) == NULL);
}

TEST(test_planet_files)
{
  static const char *names[] = {"Earth", "Earth2", "Space",   "Chemical",
                                "Death", "Warrior", "Ice", "Plastic"};
  size_t i;
  sft_resources();
  for (i = 0; i < sizeof names / sizeof *names; i++) {
    planet_data p;
    memset(&p, 0x5a, sizeof p);
    if (load_planet_file(&p, "Info/%s.info", names[i]) != &p) {
      SFT_FAIL_("%s.info did not load", names[i]);
      continue;
    }
    if ((strcmp(names[i], "Space") == 0) != (p.space_mission != 0)) {
      SFT_FAIL_("%s.info: space_mission = %d", names[i], p.space_mission);
    }
  }
}

TEST(test_default_config)
{
  game_configuration c;
  int t, k;
  sft_resources();
  REQUIRE(load_config_file(&c, "Data/Config") == &c);
  CHECK(c.version != 0);
  CHECK(c.music_volume >= 0);
  CHECK(c.sound_volume >= 0);
  /* the Cadet table seen on the title screen */
  CHECK_STR(c.high_scores[0][0].name, "HAZEL PHOENIX");
  CHECK_EQ(c.high_scores[0][0].score, 150000);
  CHECK_EQ(c.high_scores[0][4].score, 30000);
  for (t = 0; t < 4; t++) {
    for (k = 1; k < 5; k++) {
      if (c.high_scores[t][k].score > c.high_scores[t][k - 1].score) {
        SFT_FAIL_("high score table %d not descending at %d", t, k);
      }
    }
  }
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_all_missions_parse);
  RUN(test_mission_files_have_exact_size);
  RUN(test_missing_mission_fails);
  RUN(test_planet_files);
  RUN(test_default_config);
  return SFT_END();
}
