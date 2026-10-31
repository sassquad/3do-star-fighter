/* File : SF_ACCESS -> File access header file */

#ifndef __SF_ACCESS
#define __SF_ACCESS

// Includes

#include "SF_Allocation.h"
#include "SF_Mission.h"
#include "SF_Celutils.h"
#include "SF_Parameters.h"

#include <stdio.h>
#include "sf_compat.h"

// Definitions

#define ACCESS_LOADMISSION 1 // Load mission file ?
#define ACCESS_INITMISSION 2 // Init other files from mission file ?

#define DATA_CONFIGURE 1 // Load configure file flag
#define DATA_FILES 2     // Load game files flag

// External Definitions

extern char *cel_list32;                 // Pointer to 32x32 texture sprites
extern char *cel_list16;                 // Pointer to 16x16 ground sprites
extern char *cel_game;                   // Pointer to game textures list
extern char *cel_palette;                // Pointer to palette
extern char *cel_codedpalette;           // Pointer to coded6 palette
extern char *cel_plutdata;               // Pointer to plut data area
extern planet_data planet_info;          // Planet type information
extern char *map512;                     // Low res map
extern char sprite_map[256][256];        // Pointer to map [sprite] data
extern char height_map[256][256];        // Pointer to map [height] data
extern char poly_map[128][128];          // Pointer to map [polygon] data
extern char animate[2052];               // Pointer to map [animations] data
extern char cels4x4[16 * CEL_MAX4];      // Pointer to 4x4 compacted land cels
extern int32_t graphics_data[19000];     // Pointer to polygon datafiles
extern char skyfile[1024];               // Skyfile
extern mission_data mission;             // Mission data
extern cel_celdata cel_quad;             // Cel info block
extern int32_t tangent_table[1025];      // Tangent Data
extern int32_t cosine_table[2048];       // Cosine table
extern char animate_poly[1024];          // Texture animations file
extern game_configuration configuration; // Game configuration

/* Function Prototypes */

int32_t load_mission(int32_t, int32_t); // Load in a mission
int32_t load_missionfiles(void);        // Load in associated mission files
int32_t load_gamedata(int32_t); // Load in fixed game cels, cosine table etc.
int32_t load_gamecels(char *);  // Load in a set of game cels
void load_backdrop(char *, int32_t); // Load in a backdrop image

#endif
