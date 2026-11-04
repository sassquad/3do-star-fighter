#include "sf_compat.h"
/* File : Map header file */

#ifndef __SF_MAP
#define __SF_MAP

// Includes

#include "SF_Screenutils.h"
#include "SF_Celutils.h"
#include "SF_Allocation.h"
#include "SF_Mission.h"
#include "Ship_Struct.h"

// External Definitions

extern ship_list ships;
extern int32_t first_frame;
extern cel_celdata cel_quad;
extern performance_data player_performance;
extern char *pilot_names[16];
extern int32_t cheat_feature4;

// Structure definitions

typedef struct map_performance {
  int32_t icon;     // Icon to use
  int32_t bar;      // Bar icon to use
  int32_t x_pos;    // X pos of bar
  int32_t y_pos;    // Y pos of bar
  int32_t x_length; // X Length of bar
} map_performance;

// Function Prototypes

void map_show(void);    // Main map routine
void map_draw512(void); // Draw map from 512x512

void map_drawships(int32_t);   // Draw all ships onto the map
void map_drawtargets(int32_t); // Draw all targets onto the map

void map_addshipitem1(ship_stack *,
                      int32_t); // Display individual ship on map [DIRECTION /
                                // MISSION KILL STATUS]
void map_addshipitem2(ship_stack *,
                      int32_t); // Display individual ship on map [TYPE]

int32_t
map_getoffsets(int32_t *,
               int32_t *); // Centres points around player, scales to zoom
// void	planet_map_drawsmall (void);					// Draw
// small map on top right of screen
void map_showbrief(void);          // Do mission brief again
void map_backdrop(void);           // Draws map backdrop
void map_showstatus(ship_stack *); // Shows ships status

void map_statustext(map_performance *, char *,
                    int32_t);                       // Draws status info text
char *map_return_ship_title(ship_stack *, int32_t); // Return title for ship
char *map_return_ship_mode(ship_stack *);           // Return mode for ship
char *map_return_ship_killstatus(ship_stack *); // Return kill status for ship

#endif
