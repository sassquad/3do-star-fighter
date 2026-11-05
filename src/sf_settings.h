/* sf_settings.h - draw distances read from config.txt (see sf_keyconfig.h for
 * the file format).  All distances are in map squares: the map is 256 x 256
 * squares and wraps round; one square is 1 << 24 world units.
 *
 * Landscape.  The land is drawn as square grids centred on the camera, from
 * far to near, each finer one drawn over the middle of the coarser one:
 *   LAND_NEAR    full detail (one texture per square)        default 16
 *   LAND_MEDIUM  medium detail (4 x 4 squares per cell)      default 32
 *   LAND_FAR     hills (8 x 8 squares per cell): the furthest
 *                land drawn with height (mountains)          default 64
 * Beyond LAND_FAR the land is drawn flat, out to the horizon.  Each grid is
 * left out when the camera is higher than its altitude:
 *   LAND_NEAR_ALTITUDE 32, LAND_MEDIUM_ALTITUDE 48, LAND_FAR_ALTITUDE 64.
 *
 * Objects.  OBJECT_DISTANCE (default 64): big ships, smoke, lasers and
 * explosion debris are drawn within it; fighters and parachutes in 3D within
 * half of it and as small sprites beyond; ground vehicles and other ships,
 * and buildings and other ground objects, within half of it (missiles in 3D
 * within a quarter).  BUILDING_ALTITUDE (default 40): buildings and other
 * ground objects are drawn only while the camera is lower than this.  Only
 * drawing follows these distances: which ships
 * count as visible (thrusters, sounds) and which targets lock-on considers
 * keep the game's own ranges, so the game plays the same. */
#ifndef SF_SETTINGS_H
#define SF_SETTINGS_H
#include <stdio.h>

extern int sf_land_near, sf_land_medium, sf_land_far;
extern int sf_land_near_alt, sf_land_medium_alt, sf_land_far_alt;
extern int sf_object_distance, sf_building_altitude;

/* the values the game was written with */
#define SF_LAND_NEAR_DEFAULT 16
#define SF_LAND_MEDIUM_DEFAULT 32
#define SF_LAND_FAR_DEFAULT 64
#define SF_OBJECT_DISTANCE_DEFAULT 64
#define SF_BUILDING_ALTITUDE_DEFAULT 40

/* largest values (they size the buffers) */
#define SF_LAND_NEAR_MAX 32
#define SF_LAND_MEDIUM_MAX 64
#define SF_LAND_FAR_MAX 128
#define SF_OBJECT_DISTANCE_MAX 120

/* Number of draw-distance settings, and setting i's name. */
int sf_settings_count(void);
const char *sf_settings_name(int i);
/* Set setting i to v: NULL, or what is wrong with v (left unchanged). */
const char *sf_settings_set(int i, long v);
/* After all are set: NULL, or a problem between settings and (in *which)
   the setting to report it against. */
const char *sf_settings_check(int *which);
/* Write the commented settings block of config.txt with the current values. */
void sf_settings_write(FILE *f);
#endif
