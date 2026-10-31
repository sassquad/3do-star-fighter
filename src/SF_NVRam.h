#include "sf_compat.h"
/* File : NVRam manager header file */

#ifndef __SF_NVRAM
#define __SF_NVRAM

// Includes

#include "SF_Menu.h"
#include "SF_Mission.h"
#include "SF_Pyramid.h"
#include "SF_Status.h"

// External Variables

extern menu menus[MAX_MENUS];
extern pyramid pyramids[4];
extern performance_data player_performance;
extern game_status status;

// Function Prototypes

int32_t nvram_save(char *, char *, char *, int32_t); // Saves a file to NVRAM
int32_t nvram_load(char *, char *, char *, int32_t); // Loads a file from NVRAM
int32_t nvram_delete(char *, char *); // Deletes a file from NVRAM
int32_t nvram_directory(int32_t, int32_t, int32_t, int32_t,
                        char *); // Catalogues NVRAM intro the menu

#endif
