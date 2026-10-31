#include "sf_compat.h"
#include "Ship_Struct.h"
#include "Explosion_Struct.h"
#include "Graphic_Struct.h"

void ship_explosion_section_control(ship_stack *);
ship_stack *add_ship_explosion_section(ship_stack *, int32_t, int32_t);

void add_section_smoke(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                       int32_t, int32_t, int32_t, int32_t);

void explode_static_graphic(int32_t, int32_t, int32_t);
void static_explode(int32_t, int32_t);
void add_explosion_line(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                        int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                        int32_t, intptr_t, int32_t);

void explode_next_frame(int32_t);
void explosion_update(void);
void explode_ship(ship_stack *);
void dent_ground(int32_t, int32_t, int32_t);
void undent_ground(int32_t, int32_t, int32_t);

ship_stack *add_explosion_section(int32_t, int32_t, int32_t, int32_t, int32_t,
                                  int32_t, int32_t, int32_t, int32_t);

void explosion_section_control(ship_stack *);

void find_group_centre_point(collision_details_header *, ship_stack *, int32_t);

void explode_static_from_collision_box(int32_t grid_ref,
                                       int32_t explosion_velocity,
                                       intptr_t owner, int32_t style, int32_t);

void explode_ship_from_collision_box(ship_stack *, int32_t explosion_velocity,
                                     intptr_t owner, int32_t style, int32_t,
                                     int32_t);

extern explosion_list explosions;

#define PARACHUTE_NO_EXPLODE -32768

#define EXPLOSION_STYLE_NORMAL 0
#define EXPLOSION_STYLE_MEGA_BOMB 1
#define EXPLOSION_STYLE_COMET 2
#define EXPLOSION_STYLE_LOADS_OF_BITS 3

#define GROUP_0 1
#define GROUP_1 2
#define GROUP_2 4
#define GROUP_3 8
#define GROUP_4 16
#define GROUP_5 32
