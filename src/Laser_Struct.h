#include "sf_compat.h"
#ifndef __LASER_SET_H
#define __LASER_SET_H

#include "SF_ARMLink.h"

#define MAX_LASERS 256

typedef struct laser_stack 
					{	link_header header ;
						int32_t x_pos ; int32_t y_pos ; int32_t z_pos ;
						int32_t x_pos2 ; int32_t y_pos2 ; int32_t z_pos2 ;
						int32_t type ;					// byte 24
						int32_t counter ;				// byte 28
						intptr_t who_owns_me ;			// byte 32
						
						int32_t x_vel ; int32_t y_vel ; int32_t z_vel ;
					} laser_stack
					;

typedef struct laser_list {
						linked_list info ;
						laser_stack laser_item [ MAX_LASERS ] ;
						} laser_list
						;

typedef struct beam_laser_data {
								int32_t on_last_frame ;
								int32_t x_rot_bend ;
								int32_t y_rot_bend ;
								} beam_laser_data
								;



#endif
