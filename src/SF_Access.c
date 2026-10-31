// File : SF_Access
// General File Access Routines

#include "SF_Access.h"
#include "sf_load.h"
#include <stdarg.h>
#include "SF_Utility.h"
#include "SF_Music.h"
#include "SF_ARMBurn.h"
#include "SF_ARMCell.h"
#include "SF_ARMAnim.h"
#include "SF_ARMUtils.h"
#include "sf_compat.h"

/* Global variables */
void *load_mission_file(mission_data *, const char *, ...);
void *load_planet_file(planet_data *, const char *, ...);
void *load_config_file(game_configuration *, const char *, ...);

int32_t file_return;
ubyte *backdrop;

/**************************************/

int32_t
load_mission(int32_t load_level, int32_t load_mission)

// Purpose : Loads in the specified mission file & associated data files
// Accepts : pyramid level and mission number
// Returns : 0 if failed, 1 if successful

{

  // NOTE : WAIT HERE FOR MUSIC BUFFER FILL TO STOP GLITCHES IN PLAYBACK
  music_maketime();

  // Load in mission file

  if (load_mission_file(&mission, "%s%c/Miss_%d", MIS_ROOT,
                        decode_level(load_level), load_mission) == NULL) {
    return (0);
  }

  return (1);
}

/**************************************/

int32_t
load_missionfiles(void)

// Purpose : Loads in the files associated with a mission
// Accepts : nothing
// Returns : 0 if failed, 1 if successful

{
  static int32_t cache_loadlevel = -1;
  static int32_t cache_loadmission = -1;
  static char cache_planet[16] = "";
  static char cache_sky[16] = "";

  // Load in polygon map file

  if (load_fileat(*poly_map, "%sMaps/%s/%s/P_Map", MIS_ROOT, mission.location,
                  mission.variation) == NULL) {
    return (0);
  }

  // Only load in graphics and planet data if planet type is different from last
  // time

  if (strcmp(mission.planettype, cache_planet) != 0) {
    strcpy(cache_planet, mission.planettype);

    if (load_fileat_words(cel_list32, "%s%s.32", CEL_ROOT,
                          mission.planettype) == NULL) {
      return (0);
    }

    if (load_fileat_words(graphics_data, "%s%s", GRF_ROOT,
                          mission.planettype) == NULL) {
      return (0);
    }

    if (load_planet_file(&planet_info, "%s%s.info", INF_ROOT,
                         mission.planettype) == NULL) {
      return (0);
    }

    if (load_fileat_words(animate_poly, "%s%s.anim", ANIM_ROOT,
                          mission.planettype) == NULL) {
      return (0);
    }

    // Only load in these files and initialise if we are NOT in a space mission

    if (planet_info.space_mission == 0) {
      if (load_fileat_words(cel_list16, "%s%s.16", CEL_ROOT,
                            mission.planettype) == NULL) {
        return (0);
      }

      if (load_fileat_words(cels4x4, "%s%s.4", CEL_ROOT, mission.planettype) ==
          NULL) {
        return (0);
      }

      if (load_fileat_words(cel_codedpalette, "%s%s.pal", PAL_ROOT,
                            mission.planettype) == NULL) {
        return (0);
      }

      armburn_initialise(&planet_info); // Initialise new planet info
    }

    armtex_initialise(animate_poly,
                      cel_list32); // Initialise new planet texture animations
    armtex_reset(animate_poly);    // Reset all polygon animations
  }

  // ONLY LOAD IN THE FOLLOWING MISSION FILES IF THIS IS NOT A SPACE MISSION

  if (planet_info.space_mission == 0) {
    // Only load in animation if different mission from last time

    if (cache_loadlevel != mission.mission_level ||
        cache_loadmission != mission.mission_number) {
      cache_loadlevel = mission.mission_level;
      cache_loadmission = mission.mission_number;

      // NOTE : GROUND ANIMATIONS TAKEN OUT 16/8/95

      // if (load_fileat(animate,
      // "%sMaps/%s/%s/S_Anm",MIS_ROOT,mission.location, mission.variation) ==
      // NULL) 	return (0);
    }

    // Load in map for mission (Spritemap 'S_Map' and HeightMap 'H_Map')

    if (load_fileat(*sprite_map, "%sMaps/%s/%s/S_Map", MIS_ROOT,
                    mission.location, mission.variation) == NULL) {
      return (0);
    }

    if (load_fileat(*height_map, "%sMaps/%s/%s/H_Map", MIS_ROOT,
                    mission.location, mission.variation) == NULL) {
      return (0);
    }

    // Only load in sky file if it is different from last time

    if (strcmp(mission.skyfile, cache_sky) != 0) {
      strcpy(cache_sky, mission.skyfile);
      if (load_fileat_words(skyfile, "%s%s", SKY_ROOT, mission.skyfile) ==
          NULL) {
        return (0);
      }
    }

    // Gerenate maps
    arm_generatemaps(&cel_quad, *sprite_map);
  }

  return (1);
}

/**************************************/

int32_t
load_gamedata(int32_t load_spec)

// Purpose : Loads in the cosine / tangent data tables etc at game start.
// Accepts : (0) - Just load in configuration file, (1) - Load in all files,
// including configuration file Returns : 0 if failed, 1 if successful

{

  if (load_spec & DATA_CONFIGURE) {
    memset(&configuration, 0, sizeof(game_configuration));
    if (load_config_file(&configuration, "%sConfig", DAT_ROOT) ==
        NULL) { // Load in default game configuration
      return (0);
    }
  }

  if (load_spec & DATA_FILES) {
    if (load_fileat_words(cosine_table, "%sCosine", DAT_ROOT) ==
        NULL) { // Load cosine table
      return (0);
    }

    if (load_fileat_words(tangent_table, "%sTangent", DAT_ROOT) ==
        NULL) { // Load tangent table
      return (0);
    }

    if (load_fileat_words(cel_palette, "%sMonochrome.pal", PAL_ROOT) ==
        NULL) { // Load in palette sprite block for monochrome cels
      return (0);
    }
  }

  return (1);
}

/**************************************/

int32_t
load_gamecels(char *filename)

// Purpose : Loads in a set of fixed game cels (but not if already loaded)
// Accepts : Filename to load in, defaults to loading in 'game.<planettype>' if
// NULL is passed Returns : 0 if failed, 1 if successful

{

  static char cache_loadgamecel[24] = "";
  char cel_gamefile[24];

  // Loading in filename or planettype ?

  if (filename == NULL) {
    strcpy(cel_gamefile, mission.planettype);
  } else {
    strcpy(cel_gamefile, filename);
  }

  // Is file already in the cache ? - If not, load in and copy filename into
  // cache

  if (strcmp(cel_gamefile, cache_loadgamecel) != 0) {
    strcpy(cache_loadgamecel, cel_gamefile);

    // NOTE : WAIT HERE FOR MUSIC BUFFER FILL TO STOP GLITCHES IN PLAYBACK
    music_maketime();

    if (load_fileat_words(cel_quad.cel_game, "%sGame.%s", CEL_ROOT,
                          cel_gamefile) == NULL) {
      return (0);
    }
  }
  return (1);
}

/**************************************/

void
load_backdrop(char *backdrop_name, int32_t load_over)

// Purpose : Loads in a backdrop image, putting load address in global variables
// 'backdrop' - Loads in 'menu[rnd]' if NULL passed Accepts : Filename to load
// in, flag to (0) ask for buffer (1) load raw image into previously allocated
// buffer Returns : Nothing

{
  char backdrop_file[48];

  if (backdrop_name == NULL) {
    sprintf(backdrop_file, "%s%sMenu%d", RESOURCES_ROOT, IMG_ROOT,
            arm_random() & 7); // Get random menu file ?
  } else {
    sprintf(backdrop_file, "%s%s%s", RESOURCES_ROOT, IMG_ROOT,
            backdrop_name); // Or get passed image file ?
  }

  // WAIT FOR MUSIC BUFFER FILL TO STOP GLITCHES, THEN LOAD FILE

  music_maketime();

  if (backdrop == NULL) {
    backdrop = (ubyte *) calloc(
        1, 320 * 240 * 2); // allocate backdrop buffer on first use
  }
  (void) load_over;
  load_image_raw(backdrop, backdrop_file); // Or load raw image straight in ?
}

/**************************************/
/* Big-endian structure readers.
   The 3DO files hold the structures in 32-bit big-endian layout.  On this port
   'ship_sdb' contains real pointers, so mission_data cannot simply be read over
   the file image. */

static const uint8_t *rd_cur;

static uint8_t
rd8(void)
{
  return *rd_cur++;
}

static int32_t
rd32(void)
{
  int32_t v =
      (int32_t) (((uint32_t) rd_cur[0] << 24) | ((uint32_t) rd_cur[1] << 16) |
                 ((uint32_t) rd_cur[2] << 8) | rd_cur[3]);
  rd_cur += 4;
  return v;
}

static void
rdstr(void *d, int n)
{
  memcpy(d, rd_cur, (size_t) n);
  rd_cur += n;
}

static void
rd_performance(performance_data *p)
{
  int i;
  p->engine = rd32();
  p->control = rd32();
  p->shields = rd32();
  for (i = 0; i < 13; i++) {
    p->weapons[i] = rd32();
  }
  p->laser_rate = rd32();
  p->missile_rate = rd32();
  p->launch_rate = rd32();
}

#define FILE_MISSION_SIZE 4924

static int
read_whole(const char *rel, uint8_t *buf, size_t want)
{
  FILE *f = sf_fopen_resource(rel, "rb");
  size_t got;
  if (!f) {
    return 0;
  }
  got = fread(buf, 1, want, f);
  fclose(f);
  return got == want;
}

void *
load_mission_file(mission_data *m, const char *fmt, ...)
{
  char name[256];
  uint8_t buf[FILE_MISSION_SIZE];
  va_list ap;
  int i, j;
  va_start(ap, fmt);
  vsnprintf(name, sizeof name, fmt, ap);
  va_end(ap);
  if (!read_whole(name, buf, FILE_MISSION_SIZE)) {
    fprintf(stderr, "LOAD ERROR : Can't load mission '%s'\n", name);
    return NULL;
  }
  memset(m, 0, sizeof *m);
  rd_cur = buf;
  m->x_startpos = rd8();
  m->y_startpos = rd8();
  m->z_startpos = rd8();
  m->x_startrotation = rd8();
  m->free1 = rd32();
  m->free2 = rd32();
  m->starting_docked = rd8();
  m->dock_to_finish = rd8();
  m->special_setup = rd8();
  m->scanner_bust = rd8();
  m->demo_mission = rd8();
  m->free4 = rd8();
  m->free5 = rd8();
  m->free6 = rd8();
  m->mission_timer = rd32();
  m->defence_timer = rd32();
  m->difficulty = rd32();
  m->hangar_capacity = rd8();
  m->ground_lasertype = rd8();
  m->hits_allowed = rd8();
  m->shiplaunchtype_ground = rd8();
  m->shiplaunchtype_air = rd8();
  m->free14 = rd8();
  m->free15 = rd8();
  m->free16 = rd8();
  m->ship_flying = rd32();
  for (i = 0; i < 10; i++) {
    rd_performance(&m->performances[i]);
  }
  m->mission_number = rd8();
  m->mission_level = rd8();
  m->free17 = rd8();
  m->free18 = rd8();
  rdstr(m->planettype, 16);
  rdstr(m->skyfile, 16);
  m->groundcount = rd32();
  for (i = 0; i < 128; i++) {
    m->groundlist[i].xpos = rd8();
    m->groundlist[i].ypos = rd8();
    m->groundlist[i].type = rd8();
    m->groundlist[i].var = rd8();
  }
  m->flightpath_count = rd32();
  for (i = 0; i < 8; i++) {
    m->flight_paths[i].flight_points = rd32();
    for (j = 0; j < 64; j++) {
      m->flight_paths[i].flight_data[j].x_pos = rd8();
      m->flight_paths[i].flight_data[j].y_pos = rd8();
      m->flight_paths[i].flight_data[j].z_pos = rd8();
      m->flight_paths[i].flight_data[j].command = rd8();
    }
  }
  m->special_ships = rd32();
  for (i = 0; i < 32; i++) {
    ship_sdb *s = &m->ship_special[i];
    s->control_mode = rd8();
    s->alive = rd8();
    s->side = rd8();
    s->killstatus = rd8();
    s->flightpath_point = rd8();
    s->command_override = rd8();
    s->formation_position = rd8();
    s->fire_instruction = rd8();
    s->distance_check = rd8();
    s->pilot_name = rd8();
    s->cloaking_device = rd8();
    s->start_direction = rd8();
    s->free1 = rd8();
    s->bomb_todrop = rd8();
    s->bomb_timer = rd8();
    s->ship_type = rd8();
    s->init_ref = rd8();
    s->x_start = rd8();
    s->y_start = rd8();
    s->z_start = rd8();
    s->sdb_type = rd8();
    s->target_type = rd8();
    s->target_counter = rd8();
    s->max_canlaunch = rd8();
    (void) rd32();
    (void) rd32(); /* 32-bit ship_address / command_address: runtime only */
    s->ship_address = NULL;
    s->command_address = NULL;
    s->x_offset = rd32();
    s->y_offset = rd32();
    s->z_offset = rd32();
  }
  rdstr(m->title, 32);
  rdstr(m->location, 20);
  rdstr(m->variation, 20);
  if (rd_cur != buf + FILE_MISSION_SIZE) {
    fprintf(stderr, "mission parse size mismatch\n");
  }
  return m;
}

void *
load_planet_file(planet_data *p, const char *fmt, ...)
{
  char name[256];
  uint8_t buf[24];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(name, sizeof name, fmt, ap);
  va_end(ap);
  if (!read_whole(name, buf, 24)) {
    fprintf(stderr, "LOAD ERROR : Can't load '%s'\n", name);
    return NULL;
  }
  rd_cur = buf;
  p->explosion1_data1 = rd32();
  p->explosion1_data2 = rd32();
  p->explosion2_data1 = rd32();
  p->explosion2_data2 = rd32();
  p->explosion_heightcheck = rd8();
  p->space_mission = rd8();
  p->free1 = rd8();
  p->free2 = rd8();
  p->comet_rate = rd32();
  return p;
}

void *
load_config_file(game_configuration *c, const char *fmt, ...)
{
  char name[256];
  uint8_t buf[sizeof(game_configuration)];
  va_list ap;
  int i, j;
  va_start(ap, fmt);
  vsnprintf(name, sizeof name, fmt, ap);
  va_end(ap);
  if (!read_whole(name, buf, 556)) {
    fprintf(stderr, "LOAD ERROR : Can't load '%s'\n", name);
    return NULL;
  }
  memset(c, 0, sizeof *c);
  rd_cur = buf;
  c->version = rd8();
  c->free1 = rd8();
  c->free2 = rd8();
  c->free3 = rd8();
  c->language = rd8();
  c->control_method = rd8();
  c->music_volume = rd8();
  c->sound_volume = rd8();
  c->music_on = rd8();
  c->sound_on = rd8();
  c->video_on = rd8();
  c->free5 = rd8();
  rdstr(c->flight_controls, 12);
  c->stick_x_min = rd32();
  c->stick_x_max = rd32();
  c->stick_y_min = rd32();
  c->stick_y_max = rd32();
  c->stick_z_min = rd32();
  c->stick_z_max = rd32();
  rdstr(c->music_tracks, 8);
  rdstr(c->pilot, 20);
  for (i = 0; i < 4; i++) {
    for (j = 0; j < 5; j++) {
      rdstr(c->high_scores[i][j].name, 20);
      c->high_scores[i][j].score = rd32();
    }
  }
  return c;
}
