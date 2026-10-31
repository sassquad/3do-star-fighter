#include "SF_Mission.h"
#include "Ship_Struct.h"
#include "Ship_Control.h"
#include "SF_Celutils.h"
#include "sf_compat.h"
#include "Camera_Struct.h"
#include "SF_Status.h"

#include "Misc_Struct.h"
extern docking_struct docked;

extern int32_t test_mode;

/* External Definitions */

extern void rotate_node_from_c(void *);
extern int32_t wave_counter;
extern int32_t wave_counter2;
extern int32_t wave_counter3;
extern int32_t camera_x_rotation;
extern int32_t camera_y_rotation;
extern int32_t camera_x_position;
extern int32_t camera_y_position;
extern int32_t camera_z_position;
extern int32_t test_grid_flip;
extern struct camera_data camera[3];

// Camera Stuff
extern ship_stack *ship_viewed_last_frame;
extern int32_t ship_viewed_last_frame_x_rot;

extern int32_t camera_x_velocity;
extern int32_t camera_y_velocity;
extern int32_t camera_z_velocity;

extern int32_t collision_box_colour_adder;

extern game_status status;
extern int32_t pod_counter;
extern intptr_t *temp_store;
extern int32_t bonus_shade;

extern ship_stack *ship_being_viewed;

extern planet_data planet_info;

extern ship_stack *players_ship;

/* Local Function prototypes */

void draw_frame(int32_t);
