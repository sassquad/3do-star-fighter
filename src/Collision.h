#include "sf_compat.h"
#include "Ship_Struct.h"
extern int32_t check_collision(int32_t, int32_t, int32_t);
extern void setup_collision_constants(int32_t);
extern int32_t find_ground_height(int32_t, int32_t);
extern int32_t big_ship_collision_check(ship_stack *, ship_stack *);
