#include "sf_compat.h"
#ifndef __GRAPHIC_SET_H
#define __GRAPHIC_SET_H

// General structure to get addresses of data for a graphic

typedef struct graphics_details 
					{
					int32_t clip_size ;		// byte 0
					int32_t score ;			// byte 4
					int32_t graphic_adr ;		// byte 8
					int32_t explosion_adr ;	// byte 12
					
					int32_t collision_adr ;	// byte 16
					int32_t hits_counter ;		// byte 20
					int32_t x_size ;			// byte 24
					int32_t y_size ;			// byte 28
					
					int32_t z_size ;			// byte 32
					int32_t laser_data ;		// byte 36
					int32_t missile_data ;		// byte 40
					int32_t ship_data ;		// byte 44
					
					int32_t smoke_data ;		// byte 48
					int32_t thruster_data ;	// byte 52
					int32_t missile_aim ;		// byte 56
					int32_t extra_data ;		// byte 60
					
					} graphics_details
					;

// Explosion line data from graphic structure
// Used when a graphic is being blown up

typedef struct explosion_details_header 
					{
					int32_t counter ;
					int32_t type ;
					int32_t free ;
					} explosion_details_header
					;

typedef struct explosion_details 
					{
					int32_t x_pos ; int32_t y_pos ; int32_t z_pos ;
					int32_t x_pos2 ; int32_t y_pos2 ; int32_t z_pos2 ;
					int32_t colour1 ; int32_t colour2 ;
					int32_t bonus ;
					} explosion_details
					;

typedef struct collision_details_header 
					{
					int32_t counter ;
					int32_t free ;
					int32_t free2 ;
					} collision_details_header
					;

#if SF_BIG_ENDIAN
#define COLLISION_BYTES char type ; char colour ; char group ; char nowt ;
#else
/* the word is byte-swapped on little-endian hosts, so the byte order within it reverses */
#define COLLISION_BYTES char nowt ; char group ; char colour ; char type ;
#endif
typedef struct collision_details 
					{
					COLLISION_BYTES
					int32_t x_pos ; int32_t y_pos ; int32_t z_pos ;
					int32_t x_pos2 ; int32_t y_pos2 ; int32_t z_pos2 ;
					} collision_details
					;

// Smoke data from graphic structure
// Used when a ship is damaged and smoking 
// or a static graphic with a smoking chimney

typedef struct smoke_details_header 
					{
					int32_t counter ;
					} smoke_details_header
					;

typedef struct smoke_details 
					{
					int32_t x_pos ;
					int32_t y_pos ;
					int32_t z_pos ;
					} smoke_details
					;

// Thruster data from graphic structure
// Used when a ship has its thruster on

typedef struct thruster_details_header 
					{
					int32_t counter ;
					} thruster_details_header
					;

typedef struct thruster_details 
					{
					int32_t x_pos ;
					int32_t y_pos ;
					int32_t z_pos ;
					} thruster_details
					;


// Laser data from graphic structure
// Used when a ship / static graphic fires its lasers

typedef struct laser_details_header 
					{
					int32_t counter ;
					} laser_details_header
					;

typedef struct laser_details 
					{
					int32_t x_pos ;
					int32_t y_pos ;
					int32_t z_pos ;
					} laser_details
					;

// Missile data from graphic structure
// Used when a ship / static graphic fires its missiles

typedef struct missile_details_header 
					{
					int32_t counter ;
					} missile_details_header
					;

typedef struct missile_details 
					{
					int32_t x_pos ;
					int32_t y_pos ;
					int32_t z_pos ;
					} missile_details
					;

// Ship data from graphic structure
// Used when a ship / static graphic launchs a ship

typedef struct ship_details 
					{
					int32_t type ;
					int32_t x_pos ;
					int32_t y_pos ;
					int32_t z_pos ;
					} ship_details
					;

#define SHIP_SOUTH_LAUNCH	0
#define SHIP_RUNWAY_TAKEOFF	1


// Explosion section data
typedef struct section_groups 
					{
					int32_t group_0 ;
					int32_t group_1 ;
					int32_t group_2 ;
					int32_t group_3 ;
					int32_t group_4 ;
					int32_t group_5 ;
					int32_t group_6 ;
					} section_groups
					;

typedef struct section_details_header 
					{
					int32_t counter ;
					} section_details_header
					;

typedef struct section_details
					{
					int32_t type ;
					int32_t x_pos ;
					int32_t y_pos ;
					int32_t z_pos ;
					int32_t x_pos2 ;
					int32_t y_pos2 ;
					int32_t z_pos2 ;
					int32_t x_vel ;
					int32_t y_vel ;
					int32_t z_vel ;
					} section_details
					;

#define SECTION_SMALL_THRUST	0
#define SECTION_SMALL_SMOKE		1
#define SECTION_BIG_THRUST		2
#define SECTION_BIG_SMOKE		3


#endif
					

extern int32_t *static_graphics_adr ;
extern int32_t *ships_adr ;
extern int32_t *explosion_bits_adr ;
extern int32_t graphics_data [ 19000 ] ;

/* Address-like fields inside graphics_data (graphic_adr, explosion_adr, extra_data, group_N ...)
   are stored as BYTE OFFSETS from the start of graphics_data (the 3DO stored absolute 32-bit
   addresses after relocation).  Use GPTR() to turn one into a pointer (a zero offset gives NULL, meaning 'none') and GOFF() for the reverse. */
#define GPTR(off) ((int32_t)(off) ? (void*)((char*)graphics_data + (int32_t)(off)) : (void*)0)   /* 0 = 'none', as in the data files */
#define GOFF(p)   ((int32_t)((char*)(p) - (char*)graphics_data))
extern char poly_map [ 128 ] [ 128 ] ;
extern char collision_map [ 128 ] [ 128 ] ;
extern char height_map [ 256 ] [ 256 ] ;
