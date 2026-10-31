#include "sf_compat.h"
#include "SF_Mission.h"

extern planet_data planet_info;

extern int32_t camera_x_position;
extern int32_t camera_y_position;
extern int32_t camera_z_position;
extern int32_t camera_x_rotation;
extern int32_t camera_y_rotation;
extern int32_t camera_z_rotation;
extern int32_t camera_x_velocity;
extern int32_t camera_y_velocity;
extern int32_t camera_z_velocity;

extern int32_t test_mode;

extern ship_stack *ship_being_viewed;

extern int32_t land_sort_offset;

extern int32_t pex_table[16384];

extern intptr_t air_to_ground_scan_temp;
extern intptr_t air_to_ground_scan;
extern int32_t air_to_ground_x;
extern int32_t air_to_ground_y;
extern int32_t air_to_ground_z;
extern int32_t atg_selected;

extern intptr_t air_to_air_scan_temp;
extern intptr_t air_to_air_scan;
extern int32_t air_to_air_x;
extern int32_t air_to_air_y;
extern int32_t air_to_air_z;
extern int32_t ata_selected;

extern char sky[1024];
extern CCB *skycel;
extern cel_celdata cel_quad;

extern int32_t which_graphics_set;

extern void plot_stars(void);
extern void setup_rotations(void);
extern void machine_code_land_plot(void);
extern void rotate_sky(void *);
extern void plot_ship_graphic(void *);

extern char height_map[256][256];

extern intptr_t *temp_store;
extern ship_list ships;

void draw_land(void);
