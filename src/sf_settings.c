/* sf_settings.c - draw distances read from config.txt (see sf_settings.h). */
#include "sf_settings.h"
#include <string.h>

int sf_land_near = SF_LAND_NEAR_DEFAULT, sf_land_medium = SF_LAND_MEDIUM_DEFAULT,
    sf_land_far = SF_LAND_FAR_DEFAULT;
int sf_land_near_alt = 32, sf_land_medium_alt = 48, sf_land_far_alt = 64;
int sf_object_distance = SF_OBJECT_DISTANCE_DEFAULT;
int sf_building_altitude = SF_BUILDING_ALTITUDE_DEFAULT;

typedef struct {
  const char *name;
  int *value;
  int def, min, max, step;
} setting;

static const setting tab[] = {
    {"LAND_NEAR", &sf_land_near, SF_LAND_NEAR_DEFAULT, 4, SF_LAND_NEAR_MAX, 4},
    {"LAND_MEDIUM", &sf_land_medium, SF_LAND_MEDIUM_DEFAULT, 8, SF_LAND_MEDIUM_MAX, 8},
    {"LAND_FAR", &sf_land_far, SF_LAND_FAR_DEFAULT, 16, SF_LAND_FAR_MAX, 8},
    {"LAND_NEAR_ALTITUDE", &sf_land_near_alt, 32, 1, 127, 1},
    {"LAND_MEDIUM_ALTITUDE", &sf_land_medium_alt, 48, 1, 127, 1},
    {"LAND_FAR_ALTITUDE", &sf_land_far_alt, 64, 1, 127, 1},
    {"OBJECT_DISTANCE", &sf_object_distance, SF_OBJECT_DISTANCE_DEFAULT, 16,
     SF_OBJECT_DISTANCE_MAX, 8},
    {"BUILDING_ALTITUDE", &sf_building_altitude, SF_BUILDING_ALTITUDE_DEFAULT, 1, 127, 1},
};
#define NTAB ((int) (sizeof tab / sizeof *tab))

int
sf_settings_count(void)
{
  return NTAB;
}

const char *
sf_settings_name(int i)
{
  return (i >= 0 && i < NTAB) ? tab[i].name : NULL;
}

/* Set setting i to v.  Returns NULL, or what is wrong with v (the setting is
   then left as it was). */
const char *
sf_settings_set(int i, long v)
{
  static char msg[120];
  const setting *s = &tab[i];
  if (v < s->min || v > s->max || v % s->step != 0) {
    if (s->step > 1) {
      snprintf(msg, sizeof msg, "%s must be a multiple of %d from %d to %d", s->name,
               s->step, s->min, s->max);
    } else {
      snprintf(msg, sizeof msg, "%s must be a whole number from %d to %d", s->name,
               s->min, s->max);
    }
    return msg;
  }
  *s->value = (int) v;
  return NULL;
}

/* Checks between settings, after all are set.  Returns NULL, or the problem
   and (in *which) the setting it is reported against. */
const char *
sf_settings_check(int *which)
{
  if (!(sf_land_near < sf_land_medium)) {
    *which = 1;
    return "LAND_MEDIUM must be larger than LAND_NEAR";
  }
  if (!(sf_land_medium < sf_land_far)) {
    *which = 2;
    return "LAND_FAR must be larger than LAND_MEDIUM";
  }
  if (!(sf_land_near_alt <= sf_land_medium_alt)) {
    *which = 4;
    return "LAND_MEDIUM_ALTITUDE must not be lower than LAND_NEAR_ALTITUDE";
  }
  if (!(sf_land_medium_alt <= sf_land_far_alt)) {
    *which = 5;
    return "LAND_FAR_ALTITUDE must not be lower than LAND_MEDIUM_ALTITUDE";
  }
  return NULL;
}

void
sf_settings_write(FILE *f)
{
  fprintf(f,
          "\n# Draw distances, in map squares (the map is 256 x 256 squares and "
          "wraps round).\n"
          "# The landscape is drawn in full detail within LAND_NEAR (4-32, "
          "multiple of 4),\n"
          "# in medium detail within LAND_MEDIUM (8-64, multiple of 8) and with "
          "hills and\n"
          "# mountains within LAND_FAR (16-128, multiple of 8); beyond that it "
          "is drawn flat.\n"
          "# Each must be larger than the one before.  Larger distances need a "
          "faster computer.\n"
          "LAND_NEAR = %d\nLAND_MEDIUM = %d\nLAND_FAR = %d\n\n"
          "# Camera altitude, in map squares (1-127), above which each level "
          "of landscape\n"
          "# detail is no longer drawn (each no lower than the one before).\n"
          "LAND_NEAR_ALTITUDE = %d\nLAND_MEDIUM_ALTITUDE = %d\nLAND_FAR_ALTITUDE = %d\n\n"
          "# 3D objects (16-120, multiple of 8): big ships, smoke, lasers and "
          "debris are drawn\n"
          "# within OBJECT_DISTANCE; fighters, ground vehicles and buildings "
          "within half of it\n"
          "# (fighters further away as small sprites).  Lock-on and the game "
          "itself are not\n"
          "# affected.\n"
          "OBJECT_DISTANCE = %d\n\n"
          "# Camera altitude, in map squares (1-127), above which buildings and "
          "other ground\n"
          "# objects are no longer drawn.  Lock-on is not affected.\n"
          "BUILDING_ALTITUDE = %d\n",
          sf_land_near, sf_land_medium, sf_land_far, sf_land_near_alt,
          sf_land_medium_alt, sf_land_far_alt, sf_object_distance,
          sf_building_altitude);
}
