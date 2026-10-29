#include "sf_compat.h"
//	File : SF_ArmSky (Header file for sky drawing routines)

#ifndef __SF_ARMSKY
#define __SF_ARMSKY

// Function Prototypes

extern void arm_rendersky (int32_t, int32_t);		// Generate sky celdata for frame
extern void arm_plotsky (void*);			// Plot sky into temporary list

#endif
