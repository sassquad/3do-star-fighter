#include "sf_compat.h"

/* Setup data tables for quick reference look up */
extern int32_t cosine_table [ 2048 ] ;
extern int32_t pex_table [ 16384 ] ;
extern int32_t *sine_table ;
extern char polymap [ 128 ] [ 128 ] ;
extern int32_t quick_height_table [ 256 ] [ 4 ] ;

/* Setup data tables for the maps etc. */
extern char landscape_heights [ 256 ] [ 256 ] ;
extern int32_t rotated_coords [ 3200 ] [ 3 ] ;
extern int32_t screen_coords [ 3200 ] [ 2 ] ;
extern int32_t graphics_data [ 19000 ] ;
extern int32_t star_coords [ 128 ] [ 4 ] ;

extern int32_t silly_x ;
extern int32_t silly_y ;


void rotate_land( void ) ;
