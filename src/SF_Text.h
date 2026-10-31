#include "sf_compat.h"
/* File : Mission structure header file */

#ifndef __SF_MISSION
#define __SF_MISSION

/* Status structure defines */

#define GROUND_DESTROY 0 // Definitions for ground object types
#define GROUND_SWEAPONBONUS1 1
#define GROUND_SWEAPONBONUS2 2
#define GROUND_SWEAPONBONUS3 3
#define GROUND_SWEAPONBONUS4 4
#define GROUND_ADDTODEFENCETIME 5
#define GROUND_BLOWUPPREVIOUS 6
#define GROUND_NULL 7
#define GROUND_SETSHIPSHIELD 8
#define GROUND_CASHAWARD 9
#define GROUND_TOGGLETIMER 10
#define GROUND_HIT 11
#define GROUND_HIDDENDESTROY 12

#define KILL_NULL 0 // Definitions for ship kill status
#define KILL_MUSTDIE 1
#define KILL_MUSTSURVUVE 2
#define KILL_MUSTSURVIVEFLIGHT 3
#define KILL_MUSTSTOPFLIGHT 4

/* Structure definitions */

typedef struct groundobject // Ground object identifiers x,y pos, bonus etc
{
  char xpos;
  char ypos;
  char type;
  char var;
} groundobject;

typedef struct performance // Performance rating of a given ship
{
  int32_t lives;
  int32_t engine;
  int32_t control;
  int32_t shields;
  int32_t ata;
  int32_t atg;

  int32_t superweapon1;
  int32_t superweapon2;
  int32_t superweapon3;
  int32_t superweapon4;
} performance;

typedef struct mission // Complete data set for active mission
{

  // Mission start data

  int32_t x_startpos;
  int32_t y_startpos;
  int32_t z_startpos;
  int32_t start_xrotation;

  int32_t free1;
  int32_t free2;
  int32_t free3;
  int32_t free4;

  // Mission yes / no's		(0=no, 1=yes)

  char starting_docked;
  char dock_to_finish;
  char special_setup;
  char space_mission;
  char scanner_bust;
  char free5;
  char free6;
  char free7;

  char free8;
  char free9;
  char free10;
  char free11;

  // Mission timers

  int32_t missiontimer; // How int32_t to complete
  int32_t defencetimer; // How int32_t before defences active
  int32_t free12;
  int32_t free13;

  // Mission Difficulty stuff

  char difficulty;   // How difficult ?
  char hits_allowed; // How many hits allowed ?
  char free14;
  char free15;

  // Starting setup of players ship

  performance initial_setup; // Initial performance setup

  int32_t ship_flying; // Ship we are flying in this mission

  // Mission identification

  char mission_number; // 1 - 36 ?
  char mission_level;  // 0 - 3 (Training, Easy, Medium, Hard)
  char free16;
  char free17;

  // Mission filenames

  char planettype[16]; // Planet type, 'Earth', 'Chemical' etc
  char skyfile[16];    // Skyfile to use

  // Objects to destroy

  int32_t groundcount;          // Number of check objects to destroy
  groundobject groundlist[128]; // List of upto 128 items

} mission;

/* Function Prototypes */

#endif
