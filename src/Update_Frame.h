#include "sf_compat.h"

#include "Ship_Struct.h"
#include "Ship_Control.h"
#include "SF_Celutils.h"
// #include "graphics.h"
#include "Camera_Struct.h"
#include "SF_Status.h"

#include "SF_Parameters.h"
extern game_configuration configuration;

#include "Misc_Struct.h"
extern docking_struct docked;

#include "SF_Joystick.h"
extern stick_struct pro_stick;

/* External Definitions */

// bugggggg fix
extern int32_t laser_sound_counter;

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

extern game_status status;
extern int32_t pod_counter;
extern intptr_t *temp_store;

extern int32_t laser_counter;

#include "SF_Mission.h"
extern planet_data planet_info;

extern int32_t players_x_control;
extern int32_t players_y_control;
extern int32_t players_z_control;
extern int32_t players_thrust_control;

extern int32_t keypad_iomap[8];

extern int32_t bonus_collision_this_frame;

extern int32_t test_mode;

// Shitty get it ready for another show fix
extern int32_t plot_clouds_or_wot;

extern ship_stack *players_ship;

/* Local Function prototypes */

void update_frame(int32_t);
