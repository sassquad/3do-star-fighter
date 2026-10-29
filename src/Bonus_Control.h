#include "sf_compat.h"
#include "Ship_Struct.h"

extern ship_list ships ;
extern ship_stack* players_ship ;

extern int32_t bonus_collision_this_frame ;

void bonus_control( ship_stack * ) ;

void add_bonus( int32_t , int32_t , int32_t , int32_t , int32_t , int32_t , int32_t ) ;
void add_crystal_from_static( int32_t , int32_t , int32_t ) ;

void start_bonus_crystal_adder( int32_t , int32_t ) ;
void update_bonus_crystal_adder( void ) ;

void add_bonus_from_collision_box( int32_t , int32_t ) ;

#include "Misc_Struct.h"

extern bonus_crystal_adder_struct bonus_crystal_adder ;

