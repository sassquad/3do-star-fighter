#include "sf_compat.h"
/* File : SF_UTILITY -> General utilities header file */

#ifndef __SF_UTILITY
#define __SF_UTILITY

// Includes

#include <string.h>
#include <stdio.h>
#include "SF_Pyramid.h"

// External Variables

extern pyramid pyramids[4];

// Function Prototypes

char decode_level(int32_t level); // Turn a level number into a char
int32_t
return_mission(pyramid *); // Returns current mission number for a pyramid
int32_t return_missionpos(
    int32_t,
    int32_t); // Return a mission number from a passed pyramid x&y position
int32_t total(int32_t); // Does things to a number (used to get offset for ypos)
void delay_frames(int32_t frame_count); // Waits a given number of frames

#endif
