#include "sf_compat.h"
//	File : SF_ARMBurn (Header file for ground explosion routines)

#ifndef __SF_ARMBURN
#define __SF_ARMBURN
				
// Function Prototypes

extern void armburn_addexplosion (int32_t, int32_t, int32_t);		// Add a ground explosion
extern void armburn_resetexplosions (void);					// Clear all ongoing explosions
extern void armburn_updateexplosions (void*, void*);		// Update all explosions
extern void armburn_initialise (void*);						// Initialise explosion sprite info

#endif
