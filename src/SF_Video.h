#include "sf_compat.h"
/* File : Video Player header file */

#ifndef __SF_VIDEO
#define __SF_VIDEO

// Includes

#include "SF_Screenutils.h"

// External Variables

extern ScreenContext *screen;

// Function Prototypes

int32_t video_play(char *video_file); // Play a Cinepak video sequence

#endif
