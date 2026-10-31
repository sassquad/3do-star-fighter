#include "sf_compat.h"
/* File : Status header file */

#ifndef __SF_STATUS
#define __SF_STATUS

// Includes

#include "SF_Mission.h"
#include "SF_Bonus.h"
#include "Ship_Struct.h"

// Status structure defines

#define STATUS_ZOOMIN 0           // We are zooming in camera at mission start
#define STATUS_CONTINUE 1         // Game in normal progress
#define STATUS_MISSIONCOMPLETED 2 // Mission completed
#define STATUS_CRASHED 3          // Player has crashed
#define STATUS_GAMEOVERWAIT 4     // We are waiting for game over
#define STATUS_GAMEOVER 5         // Game is over
#define STATUS_MISSIONCOMPLETEDWAIT 6 // Waiting to do mission completed

#define MODE_NORMAL 0 // Formation control modes
#define MODE_AGGRESSIVE 1
#define MODE_DEFENSIVE 2

#define TRACKING_NOTHING 0 // Type of object we are tracking
#define TRACKING_SHIP 1
#define TRACKING_GROUND 2

#define TRACKING_STATUS_FREE                                                   \
  0 // Status of tracking (free or locked onto 1 object ?)
#define TRACKING_STATUS_LOCKED 1

// Structure definitions

typedef struct game_status {
  int32_t status;           // Current status
  int32_t status_count;     // Current status counter
  int32_t status_decrement; // Ammount to decrement counter by

  int32_t current_camera; // Current camera being used
  int32_t current_status; // Current state of play

  int32_t bonus_type; // Are we on a bonus ?
  int32_t clock1;     // Timer 1
  int32_t clock2;     // Timer 2

  int32_t
      friendly_fire; // Number of lasers fired upon friendly craft this mission
  int32_t enemy_aircount;    // Number of air enemy targets destroyed
  int32_t enemy_groundcount; // Number of ground enemy targets destroyed
  int32_t hits_taken;        // Number of hits taken

  int32_t score;  // Players score
  int32_t clock3; // Occurences of friendly fire this mission

  int32_t lives;          // Number of lives left
  int32_t weapon;         // Currently selected weapon
  int32_t formation_mode; // Current formation control mode

  char flag_allgrounddead; // Have all ground objects been destroyed ?
  char flag_allshipdead;   // Have all aerial objects been destroyed ?
  char flag_defendland;    // Has the ship we were defending landed ?
  char flag_waitingtodock; // Are we waiting to dock (ie. mission completed
                           // otherwise ?)

  char pickup_count;  // Current pickup status
  char pickups[4];    // Current pickups
  char fast_crystals; // Fast crystals to add
  char mega_ship;     // Counter for megaship

  uint8_t chain_next;      // Pointer to next in list to destroy
  char chain_timer;        // Timer before next destroyed
  char display_lives;      // Are we displaying number of lives ?
  char missions_completed; // Number of missions completed by player

  char track_type;   // Type of object we are tracking
  char track_status; // Status of tracking
  char track_xpos;   // X pos of ground object we are tracking
  char track_ypos;   // X pos of ground object we are tracking

  ship_stack *docked;        // Ship we are docked in (or NULL if not)
  ship_stack *ship_tracking; // Ship we are tracking

} game_status;

// External Definitions

extern game_status status;
extern mission_data mission;
extern performance_data player_performance;
extern ship_stack *players_ship;
extern ship_list ships;
extern planet_data planet_info;
extern int32_t player_crashed_xpos;
extern int32_t player_crashed_ypos;
extern int32_t player_crashed_zpos;

// Function Definitions

void status_register(int32_t, int32_t,
                     int32_t); // Register a new game status + counter start
                               // value + count dec value
void status_updatestatus(
    void); // (Called If status counter>0), update current game status
void status_groundhit(int32_t, int32_t); // Check a ground object destroyed
void status_awardmissionbonus(
    int32_t, int32_t, int32_t, int32_t, int32_t,
    int32_t); // Awards bonus from ground object or ship

#endif
