#include "sf_compat.h"
//	File : SF_ARMUtils (Header file for utility routines)

#ifndef __SF_ARMUTILS
#define __SF_ARMUTILS

// Function Prototypes

extern	void	arm_randominit (void);							// Reset random seed values
extern	int32_t	arm_random (void);								// Get 32bit random value
extern	int32_t	arm_randomvalue (int32_t);							// Returns random number between 0 & passed value (inclusive)
extern	void	arm_drawmaptargets (void*, int32_t, void*, int32_t);	// Draws all map objects

#endif
