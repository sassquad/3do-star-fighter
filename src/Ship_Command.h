#include "sf_compat.h"
#include "Ship_Struct.h"

void fast_fighter_control(ship_stack *);

void slow_fighter_control(ship_stack *, int32_t, int32_t, int32_t);

void small_ship_command_update(ship_stack *);

void small_fighter_damage_wobble(ship_stack *, int32_t);

void small_ship_damaged(ship_stack *);
