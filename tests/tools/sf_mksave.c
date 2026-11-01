/* sf_mksave - derive a save game that starts at a chosen mission.
 *
 *   sf_mksave BASE OUT LEVEL MISSION
 *
 * BASE is a save file written by the game (tests/fixtures/base.save, made by
 * `tests/run.py make-fixtures`); OUT gets the same contents with the current
 * level set to LEVEL (0 = T, 1 = E, 2 = M, 3 = H) and that level's pyramid
 * cursor on MISSION (1..15, the number in Missions/<L>/MISS_<n>).  Loading
 * OUT and pressing A on the mission-select screen starts that mission.
 *
 * The save format is the game's in-memory 'gamefile' struct, so this tool is
 * built from the game's own headers. */
#include "sf_compat.h"
#include "SF_Menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAMEFILE_VERSION 2 /* as in SF_Menu.c */

int
main(int argc, char **argv)
{
  gamefile g;
  FILE *f;
  long level, mission, y, idx;
  if (argc != 5) {
    fprintf(stderr, "usage: %s BASE OUT LEVEL MISSION\n", argv[0]);
    return 2;
  }
  level = strtol(argv[3], NULL, 10);
  mission = strtol(argv[4], NULL, 10);
  if (level < 0 || level > 3 || mission < 1 || mission > MAX_MISSIONS) {
    fprintf(stderr, "sf_mksave: level 0..3, mission 1..%d\n", MAX_MISSIONS);
    return 2;
  }
  f = fopen(argv[1], "rb");
  if (!f || fread(&g, 1, sizeof g, f) != sizeof g || fgetc(f) != EOF) {
    fprintf(stderr, "sf_mksave: %s is not a %zu-byte save file\n", argv[1],
            sizeof g);
    return 1;
  }
  fclose(f);
  if (g.savefile_version != GAMEFILE_VERSION) {
    fprintf(stderr, "sf_mksave: %s has save version %d, expected %d\n",
            argv[1], g.savefile_version, GAMEFILE_VERSION);
    return 1;
  }
  /* pyramid position: mission index = x + y*(y+1)/2, row y holds y+1 */
  idx = mission - 1;
  for (y = 0; (y + 1) * (y + 2) / 2 <= idx; y++) {
  }
  g.savefile_level = (int32_t) level;
  g.savefile_pyramids[level].y_level = (int32_t) y;
  g.savefile_pyramids[level].x_level = (int32_t) (idx - y * (y + 1) / 2);
  g.savefile_pyramids[level].mission[idx] = MISSION_UNFINISHED;
  f = fopen(argv[2], "wb");
  if (!f || fwrite(&g, 1, sizeof g, f) != sizeof g || fclose(f) != 0) {
    fprintf(stderr, "sf_mksave: cannot write %s\n", argv[2]);
    return 1;
  }
  return 0;
}
