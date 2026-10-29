#include "sf_compat.h"
// File : Bonus header file

#ifndef __SF_BONUS
#define __SF_BONUS

// Includes

#include "SF_Mission.h"
#include "Ship_Struct.h"

// Crystal Definitions

#define	C_NONE		0
#define C_RED		1
#define C_GREEN		2
#define C_YELLOW	3
#define C_BLUE		4
#define C_MAGENTA	5
#define C_CYAN		6
#define C_ORANGE	7
#define C_DARK		8

/**************************************/

// Structure definitions for award crystals

typedef	int32_t	(*CrystalFunction) ();

typedef struct	crystal_award								// Definition of awards structure
				{
				char			award [4];
				CrystalFunction	function;
				int32_t			parameter1;
				int32_t			parameter2;
				int32_t			parameter3;
				} crystal_award;


// External variables

extern	performance_data	player_performance;	
extern	performance_data	player_megaperformance;
extern	ship_stack 			*players_ship;
extern	ship_list			ships;

// Function Prototypes

void	bonus_parachute (int32_t);								// Add a random parachute bonus
void	bonus_addpickup (int32_t	pickup_type);				// Add a crystal pickup

// Function Prototypes for crystal bonus awards - All accept 1 int32_t, 1 char, 1 int32_t as a parameter (from award)

int32_t	bonus_weapon (int32_t, int32_t, int32_t);					// Award weapon upgrade
int32_t	bonus_wingpod (int32_t, int32_t, int32_t);					// Award wingpod
int32_t	bonus_performance (int32_t, int32_t, int32_t);				// Award engine / control / shields
int32_t	bonus_fastcrystal (int32_t, int32_t, int32_t);				// Award fast adder crystals

int32_t	bonus_megasetup_start (int32_t, int32_t, int32_t);			// Award megaship for a certain ammount of time
void	bonus_megasetup_cancel (void);						// Reset player from megaship
void	bonus_deletewingpods (void);						// Routine to delete all wingpods

void	bonus_downgrade_ship (int32_t damage_rating);			// Picks a thing randomly & downgrades it, displays message etc.
#endif
