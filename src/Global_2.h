#include "sf_compat.h"

// Setup various structures
#include "Ship_Struct.h"
ship_list ships;
ship_stack *players_ship;

#include "Smoke_Struct.h"
smoke_list smokes;

#include "Camera_Struct.h"
camera_data camera[MAX_CAMERAS];

#include "Laser_Struct.h"
laser_list lasers;

laser_stack *beam_lasers[32];
beam_laser_data beam_laser;

#include "Bit_Struct.h"
bit_list bits;

#include "Explosion_Struct.h"
explosion_list explosions;

#include "Misc_Struct.h"

// Docking stuff for the players ship
docking_struct docked;

// Bonus crystal stuff
bonus_crystal_adder_struct bonus_crystal_adder;
bonus_crystal_hill_struct bonus_crystal_hill;
bonus_crystal_object_struct bonus_crystal_object;
int32_t bonus_collision_this_frame = 0;

// Players control rates
int32_t players_x_control = 0;
int32_t players_y_control = 0;
int32_t players_z_control = 0;
int32_t players_thrust_control = 0;

// ########### bug fix for sound_conteol
int32_t laser_sound_counter = 0;

// Two nearest ships of this type make engine sounds
ship_stack *sound_big_ship;
ship_stack *sound_small_ship;
int32_t sound_channel_small_ship = -1;
int32_t sound_channel_big_ship = -1;
int32_t sound_sample_small_ship = 0;
int32_t sound_channel_shields_low = -1;
int32_t sound_channel_beam_laser = -1;
int32_t engine_sounds_on_or_wot = 0;

ship_stack *ship_being_viewed = 0;

// Rate of fire for ground objects - chances in 1024
int32_t ground_laser_rate = 512;
int32_t ground_sam_rate = 0;
int32_t ground_ship_rate = 8;

int32_t ground_laser_type = 2;
int32_t ground_ship_type = 0;
int32_t air_ship_type = 0;

// Toggle for the ground lasers
int32_t laser_counter;

int32_t test_mode = 0;

// Misc counters etc.
int32_t pod_counter = 0;

int32_t which_graphics_set;

int32_t ship_viewed_last_frame_x_rot;
ship_stack *ship_viewed_last_frame;

// Setup data tables for quick reference look up
int32_t cosine_table[2048];
int32_t *sine_table = &cosine_table[256 * 3];
int32_t pex_table[16384];
int32_t pex_table_near[2048];
int32_t quick_height_table[256][4];
int32_t tangent_table[1025];

// Collision table for static ground obejcts
char collision_map[128][128];

// Setup data tables for the stars
int32_t star_coords[128][4];

// Setup temp workspace for rotated coords and screen coords

// For use by the landscape rotator
int32_t rotated_coords[3200][3];
int32_t screen_coords[3200][2];

// For use by the graphics - ships , static objects , explosion bits etc.
int32_t graphic_rotated_coords[256][4];
int32_t graphic_screen_coords[256][2];

// Temp store for misc use
static intptr_t
    temp_store_area[2048]; /* scratch (was aliased over graphic_rotated_coords
                              on 3DO; pointer-sized now) */
intptr_t *temp_store = temp_store_area;

// General store for all polygon graphics - ships , static objects , explosion
// bits This contains all coord data + polygon link data All explosion data and
// collision data Misc items such as score / hits counter etc.
int32_t graphics_data[19000];

// Global pointers to items in the graphics table
int32_t *static_graphics_adr;
int32_t *ships_adr;
int32_t *explosion_bits_adr;

// Is the 3DO quicker than the arm tests
// mat33f16 matrix ;
// vec3f16 pre_rot [ 128 ] ;
// vec3f16 post_rot [ 128 ] ;
