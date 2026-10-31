#include "sf_compat.h"
/* File : Pyramid header file */

#ifndef __SF_PYRAMID
#define __SF_PYRAMID

// Includes

#include "SF_Celutils.h"

// Definitions

#define MAX_MISSIONS 15      // Max number of missions per level
#define MISSION_UNFINISHED 0 // Mission unfinished flag
#define MISSION_FINISHED 1   // Mission finished flag

#define PYRAMID_START 0    // Selection has been made
#define PYRAMID_CONTINUE 1 // No Selection has been made yet
#define PYRAMID_QUIT 2     // Player quitted pyramid

// Icon definitions for gameset 'Pyramid'

#define BASECEL_PYRAMID_PLANETS 0
#define BASECEL_PYRAMID_POINTER 32
#define BASECEL_PYRAMID_NUMBERS 36
#define BASECEL_PYRAMID_COMPLETED 51
#define BASECEL_PYRAMID_LEVEL 56

// Mission description structure definition

typedef struct pyramid // Pyramid structure data
{
  int32_t sub_levels;         // Max no. of sub-levels in pyramid
  int32_t y_level;            // Current sub-level number
  int32_t x_level;            // Current mission in sub-level
  char mission[MAX_MISSIONS]; // Array of missions in pyramid
} pyramid;

// External Definitions

extern cel_celdata cel_quad;
extern int32_t cosine_table[2048];
extern int32_t *sine_table;
extern int32_t cheat_feature1;

// Function Prototypes

void pyramid_initialiselevel(int32_t);   // Clear a pyramid level
void pyramid_initialisegame(void);       // Clear all pyramid levels
void pyramid_initialiseselect(int32_t);  // Initialise selection on a pyramid
int32_t pyramid_read(int32_t, int32_t);  // Update a selection on a pyramid
void pyramid_showmission(void);          // Display mission description
int32_t pyramid_registercompleted(void); // Register a mission as completed

#endif
