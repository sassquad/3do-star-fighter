#include "sf_compat.h"
/* File : Game Parameters header file */

#ifndef __SF_PARAMETERS
#define __SF_PARAMETERS

// Definitions

#define MENUSWITCH_TIMEOUT 2048

// Structure Definitions

typedef struct game_parameters {
  int32_t level;
  int32_t menu_display;
  int32_t display_timer;
  int32_t demo_counter;
  int32_t demo_or_credits;
  int32_t display_hiscoretable;
} game_parameters;

typedef struct high_score {
  char name[20];
  int32_t score;
} high_score;

typedef struct game_configuration {

  char version;
  char free1;
  char free2;
  char free3;

  char language;
  uint8_t control_method;
  char music_volume;
  char sound_volume;

  char music_on;
  char sound_on;
  char video_on;
  char free5;

  unsigned char flight_controls[12];

  int32_t stick_x_min;
  int32_t stick_x_max;
  int32_t stick_y_min;
  int32_t stick_y_max;
  int32_t stick_z_min;
  int32_t stick_z_max;

  char music_tracks[8];
  char pilot[20];
  high_score high_scores[4][5];
} game_configuration;

#endif
