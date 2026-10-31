#include "sf_compat.h"
#ifndef __MISC_SET_H
#define __MISC_SET_H

#include "Ship_Struct.h"

// Structure for preparing the landscape node rotations
typedef struct rot_struct {
  int32_t x_pos;
  int32_t y_pos;
  int32_t offset_x;
  int32_t offset_y;
  int32_t offset_z;
} rot_struct;

// Structure for finding target rotations based on position
// of item to fire from and item to fire at
typedef struct target_struct {
  int32_t x_pos;
  int32_t y_pos;
  int32_t z_pos;
  int32_t x_aim;
  int32_t y_aim;
  int32_t z_aim;
  int32_t x_rot;
  int32_t y_rot;
  int32_t distance;
} target_struct;

// Structure for the players docking details
typedef struct docking_struct {
  int32_t docked_this_frame;
  ship_stack *ship;
  int32_t counter;
  int32_t status;
} docking_struct;

#define DOCKING_OUT 0
#define DOCKING_ENTER 1
#define DOCKING_EXIT 2
#define DOCKING_FIXED 3

// Structure for bonus crystal adding
typedef struct bonus_crystal_adder_struct {
  int32_t x_pos;
  int32_t y_pos;
  int32_t z_pos;
  int32_t counter;
} bonus_crystal_adder_struct;

// Structure for bonus crystal under hills
typedef struct bonus_crystal_hill_struct {
  int32_t x_grid_1;
  int32_t y_grid_1;
  int32_t x_grid_2;
  int32_t y_grid_2;
  int32_t x_grid_3;
  int32_t y_grid_3;
  int32_t x_grid_4;
  int32_t y_grid_4;
} bonus_crystal_hill_struct;

// Structure for bonus crystal object destroying
typedef struct bonus_crystal_object_struct {
  int32_t object_1;
  int32_t object_2;
  int32_t object_3;
  int32_t object_4;
} bonus_crystal_object_struct;

#define ROT_LIMIT ((1024 * 1024) - 1)

#endif
