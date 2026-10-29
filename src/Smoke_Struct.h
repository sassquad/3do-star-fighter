#include "sf_compat.h"
#ifndef __SMOKE_SET_H
#define __SMOKE_SET_H

#include "SF_ARMLink.h"

#define MAX_SMOKES 128

typedef struct smoke_stack 
					{	link_header header ;
						int32_t x_pos ; int32_t y_pos ; int32_t z_pos ;	// bytes 0-11
						int32_t x_vel ; int32_t y_vel ; int32_t z_vel ;	// bytes 12-23
						int32_t counter ;							// bytes 24-27
						int32_t colour ;							// bytes 28-31
						int32_t type ;								// bytes 32-35
					} smoke_stack
					;

typedef struct smoke_list {
						linked_list info ;
						smoke_stack smoke_item [ MAX_SMOKES ] ;
						} smoke_list
						;

#endif
