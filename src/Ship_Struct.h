#include "sf_compat.h"

#ifndef __SHIP_SET_H
#define __SHIP_SET_H

#include "SF_ARMLink.h"
#include "SF_Mission.h"

#define MAX_SHIPS 64

#define PLAYERS_SHIP 0
#define SMALL_SHIP 1
#define BIG_SHIP 2
#define BONUS 3
#define PARACHUTE 4
#define SATELLITE 5
#define WEAPON 6
#define CAR 7

#define SECTION 256
#define SHIP_SECTION 512

#define MOTHER_SHIP ((2 << 4) + 1)

#define WEAPON_NOTHING 255
#define WEAPON_LASER 0
#define WEAPON_ATG 1
#define WEAPON_ATA 2
#define WEAPON_MEGA_BOMB 3
#define WEAPON_BEAM_LASER 4
#define WEAPON_MULTI_MISSILE 5
#define WEAPON_MINE 6
#define WEAPON_ECM 7
// 8,9,10 THESE WEAPONS ARE FREE
#define WEAPON_WING_POD 11
#define WEAPON_LAUNCH_SHIP 12

// #define WEAPON_
// #define WEAPON_

#define SHIP_CONTROL_RUN_AWAY 0
#define SHIP_CONTROL_ATTACK 1

#define COMMAND_NOTHING 0
#define COMMAND_TAXI_TURN 1
#define COMMAND_TAXI_TAKEOFF 2
#define COMMAND_TAKEOFF_CLIMB 3
#define COMMAND_BIGSHIP_LAUNCH 4
#define COMMAND_SLOW_TURN 5
#define COMMAND_FAST_TURN 6
#define COMMAND_LOOP 7
#define COMMAND_ROLL_LEFT 8
#define COMMAND_ROLL_RIGHT 9
#define COMMAND_ATTACK_MODE_1 10
#define COMMAND_RUN_AWAY 11
#define COMMAND_FLIGHT_PATH 12
#define COMMAND_FIXED_FORMATION 13
#define COMMAND_BREAK_FORMATION 14
#define COMMAND_ENTER_FORMATION 15
#define COMMAND_AVOID_HILLS 16

#define COMMAND_ATTACK_FAR 17
#define COMMAND_ATTACK_PATH 18
#define COMMAND_ATTACK 19
#define COMMAND_ATTACK_RUN_AWAY 20

#define COMMAND_NOTHING_COUNTER 0
#define COMMAND_TAXI_TURN_COUNTER 32
#define COMMAND_TAXI_TAKEOFF_COUNTER 32
#define COMMAND_TAKEOFF_CLIMB_COUNTER 32
#define COMMAND_BIGSHIP_LAUNCH_COUNTER 32
#define COMMAND_SLOW_TURN_COUNTER 32
#define COMMAND_FAST_TURN_COUNTER 32
#define COMMAND_LOOP_COUNTER 32
#define COMMAND_ROLL_COUNTER 32
#define COMMAND_BREAK_FORMATION_COUNTER 16

#define DAMAGE_NOTHING 0
#define DAMAGE_SLIGHT 1
#define DAMAGE_MEDIUM 2
#define DAMAGE_BAD 3
#define DAMAGE_VERY_BAD 4

#define DAMAGE_NOTHING_COUNTER 0
#define DAMAGE_SLIGHT_COUNTER 30
#define DAMAGE_MEDIUM_COUNTER 50
#define DAMAGE_BAD_COUNTER 70
#define DAMAGE_VERY_BAD_COUNTER 90

#define DAMAGE_HILL_BOUNCE_BIT (1 << 8)

typedef struct ship_stack {
  link_header header;

  // Keep the first items in the same place as they
  // are accessed from machine code

  // Centre point position and rotation for all objects of this type
  int32_t x_pos;
  int32_t y_pos;
  int32_t z_pos; // bytes 0,4,8
  int32_t x_rot;
  int32_t y_rot;
  int32_t z_rot; // bytes 12,16,20

  // Which graphic is it
  int32_t type; // byte 24
  // If <= 255 then bits 4-6 ref which ship sub group
  //
  // bits		6	5	4
  //			---------
  //			0	0	0	players ship
  //			0	0	1	small ships
  //			0	1	0	big ships
  //			0	1	1	bonus
  //			1	0	0	parachute / misc
  //			1	0	1	satellite
  //			1	1	0	weapons
  //			1	1	1	vechical

  // bits 0-3 ref which item number in sub group

  // if >=256 then plot static graphic of ref -255
  // this is used for the explosions when a building splits into bits

  // These are used for collision detection
  int32_t collision_size; // byte 28
  intptr_t who_owns_me;   // byte 32
  intptr_t what_hit_me;   // byte 36
  int32_t group_mask;     // byte 40

  // Misc counter used for fire requests to stop lasers firing
  // every frame etc.
  // Also used as the plot group bit mask for explosion bits NOT ANYMORE
  // !!!!!!!!!!
  int32_t counter; // byte 44

  int32_t ref;         // byte 48
  intptr_t who_hit_me; // byte 52

  int32_t fire_request;      // byte 56
  int32_t last_fire_request; // byte 60

  int32_t x_dir;
  int32_t y_dir;          // byte 64,68
  int32_t thrust_control; // byte 72
  int32_t x_control;
  int32_t y_control;
  int32_t z_control; // bytes 76 , 80 , 84
  int32_t speed;     // byte 88
  int32_t x_vel;
  int32_t y_vel;
  int32_t z_vel;                 // byte 92 , 96 , 100
  performance_data *performance; // byte 104
  int32_t can_see;               // byte 108
  ship_sdb *special_data;        // byte 112
  int32_t shields;
  void *target;
  void *target2;
  void *target3;
  int32_t goto_x;
  int32_t goto_y;
  int32_t goto_z;
  int32_t command;
  int32_t command_counter;

  void *attacker;
  int32_t attacker_rating;

  int32_t misc_counter;

  int32_t aim_counter;
  int32_t aim_goto_x;
  int32_t aim_goto_y;
  int32_t aim_goto_z;

  int32_t aim_x_rot;
  int32_t aim_y_rot;
  int32_t aim_z_rot;

  int32_t aim_x_vel;
  int32_t aim_y_vel;
  int32_t aim_z_vel;

  int32_t z_roller;
  int32_t damage;
  int32_t damage_counter;

} ship_stack;

typedef struct ship_list {
  linked_list info;
  ship_stack ship_item[MAX_SHIPS];
} ship_list;

#endif
