#include "sf_compat.h"

extern void rotate_2d_node(void *);
extern void rotate_node_from_c(void *);
extern void rotate_land_node_from_c(void *);
extern int32_t find_rotation(int32_t, int32_t);
extern int32_t find_2d_distance(int32_t, int32_t);
extern void target_finder(void *);
extern void mc_smoke_mover(void *);
extern void scan_poly_map(int32_t, int32_t, void *);
extern void scan_poly_map_2(int32_t, int32_t, void *);
extern void scan_poly_map_window(int32_t, int32_t, void *, int32_t);

#ifndef MATHS_STUFF_EXTRA
#define MATHS_STUFF_EXTRA
extern int32_t divide(int32_t, int32_t);
extern void rotate_coords_x(int32_t *x, int32_t *y, int32_t *z, int32_t ang);
extern void rotate_coords_y(int32_t *x, int32_t *y, int32_t *z, int32_t ang);
extern void rotate_coords_z(int32_t *x, int32_t *y, int32_t *z, int32_t ang);
extern void setup_camera_rotations(void);
extern void rotate_node_x_y_z_camera(int32_t *x, int32_t *y, int32_t *z);
extern void oppo_rotate_node_from_c(void *);
#endif
