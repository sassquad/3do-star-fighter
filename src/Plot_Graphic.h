#include "sf_compat.h"
extern int32_t camera_x_rotation;
extern int32_t camera_y_rotation;
extern int32_t camera_z_rotation;
extern int32_t camera_x_position;
extern int32_t camera_y_position;
extern int32_t camera_z_position;
extern int32_t camera_x_velocity;
extern int32_t camera_y_velocity;
extern int32_t camera_z_velocity;

extern int32_t atg_selected;
extern int32_t ata_selected;

extern void plot_stars(void);
extern void plot_space_stars(void);
extern void setup_rotations(void);
extern void plot_spinning_ship(int32_t, int32_t);

extern void plot_ship_graphic(void *);
extern void plot_bit_graphic(void *);
extern void rotate_node_from_c(void *);
extern void plot_smoke(void *);
extern void machine_code_constants(void *);
extern void rotate_sky(void *);
extern int32_t find_rotation(int32_t, int32_t);
extern void plot_laser(void *);
extern void plot_bit(void *);
extern void rotate_sky_node(void *);
extern void plot_planets(void);

/* Flags added to an object's z-sort ref by draw_land (see plot_sorted_graphics):
   draw a ship as a distant sprite; the object is beyond the game's own range
   (drawn because of a larger OBJECT_DISTANCE): not a lock-on candidate. */
#define PLOT_AS_SPRITE 16
#define PLOT_NO_LOCKON 32
extern int32_t plot_ship_sprite, plot_lockon_ok;

#ifndef __GRAPHIC_STUFF_H
#define __GRAPHIC_STUFF_H

typedef struct rotate_node {
  int32_t x_pos;
  int32_t y_pos;
  int32_t z_pos;
  int32_t x_rot;
  int32_t y_rot;
  int32_t z_rot;
} rotate_node;

#endif
