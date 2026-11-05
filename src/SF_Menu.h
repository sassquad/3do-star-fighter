/* File : SF_MENU -> Menus header file */

#ifndef __SF_MENU
#define __SF_MENU

/* Includes */

#include "sf_compat.h"

#include "SF_Celutils.h"
#include "SF_Allocation.h"
#include "SF_Status.h"
#include "SF_Parameters.h"
#include "SF_Music.h"
#include "SF_Pyramid.h"
#include "SF_Mission.h"
#include "SF_Joystick.h"

#include "Camera_Struct.h"
#include "Ship_Struct.h"

// Definitions for user messages

#define USER_NVRAMFULL 1
#define USER_DEFAULTCONFIG 2

// Definitions for easy access to menu variables

#define NO_MENU -1
#define MAIN_MENU 0
#define FORMATION_MENU 1
#define PAUSE_MENU 2
#define PLAYERPOS_MENU 3
#define GAMEPOS_MENU 4
#define MISSION_MENU 5
#define LEVEL_MENU 6
#define SOUND_MENU 7
#define JUKEBOX_MENU 8
#define LOAD_MENU 9
#define SAVE_MENU 10
#define CONFIGURE_MENU 11
#define CONTROLS_MENU 12
#define DEBRIEF_MENU 13
#define LANGUAGE_MENU 14
#define HIGHSCORE_MENU 15
#define FLIGHTCONTROL_MENU 16
#define CALIBRATE_JOYSTICK_MENU 17

// Definitions for easy access to menu commands

#define MAIN_NOTHING 0
#define MAIN_START 1
#define MAIN_LOADSTART 2

// Definitions for menu item status flags

#define ITEM_NOTOK 0 // Item is shaded and cannot be selected
#define ITEM_OK 1    // Item can be selected

#define ITEM_TN 0 // Item cannot be ticked
#define ITEM_TE 1 // Item can be exclusively ticked
#define ITEM_TM_C                                                              \
  2 // Item can be ticked along with others, but is not currently
#define ITEM_TM_S 3 // Item can be ticked along with others, and is set

#define ITEM_PR 1 // Plot Icon on right
#define ITEM_PL 2 // Plot Icon on left
#define ITEM_PB 3 // Plot Icon on right and left
#define ITEM_PA 4 // Always plot icon (Do not link to ticks)

/* Structure Definitions */

typedef struct gamefile // Save file structure
{
  int32_t savefile_version;              // Current version of file
  int32_t savefile_level;                // Current level
  pyramid savefile_pyramids[4];          // Pyramids status
  performance_data savefile_performance; // Players ship performance
  game_status savefile_status;           // Players play status
  camera_data savefile_cameras[2];       // Players camera settings
} gamefile;

typedef void (*MenuFunction)(int32_t, int32_t);

typedef struct menu_info {
  int32_t current_menu, command, selected, hidden;
  CCB *cel_textccb;
} menu_info;

typedef struct menu_item {
  int32_t item_icon;
  int32_t item_iconwidth;
  int32_t item_plottype;
  int32_t tick_status;
  int32_t y_position;
  int32_t selectable;
  int32_t command;

  MenuFunction function;
  int32_t function_value;

  int32_t max_selection;
  int32_t cur_selection;

  int32_t item_colour;
  int32_t string[4];
  TextCel *tccbptr;
} menu_item;

typedef struct menu_border {
  int32_t colour;
  char option_start;
  char option_end;
} menu_border;

typedef struct menu {
  int32_t title;
  int32_t reload_gamesprites;
  char *gamesprites;
  int32_t quit;
  int32_t title_colour;
  int32_t tick_position;

  int32_t ypos;

  int32_t previous;
  int32_t border_count;
  menu_border borders[4];
  int32_t max_option;
  int32_t cur_option;

  menu_item items[9];
} menu;

/* External definitions */

extern int32_t first_frame;
extern int32_t music_background;
extern int32_t info;
extern int32_t configure_waiting;

extern menu_info menu_status;
extern menu menus[MAX_MENUS];
extern TextCel *cel_text[FONT_MAXMENU];
extern game_status status;
extern camera_data camera[MAX_CAMERAS];
extern ship_list ships;
extern ship_stack *players_ship;
extern game_parameters parameters;
extern game_configuration configuration;
extern performance_data player_performance;
extern cel_celdata cel_quad;
extern pyramid pyramids[4];

extern char MUSIC_PROGRAM[MUSIC_PLAYLISTLENGTH];
extern char MUSIC_PROGRAMLENGTH;
extern int32_t cheat_feature3;
extern stick_struct pro_stick;

extern int32_t cheat_feature1;
extern int32_t cheat_feature2;
extern int32_t cheat_feature3;
extern int32_t cheat_feature4;

/* Function Prototypes */

// Menu control functions

void menu_moveup(int32_t, int32_t);    // Move to previous menu
void menu_movedown(int32_t, int32_t);  // Move to sub menu
void menu_enter(int32_t, int32_t);     // Set up a new menu
int32_t menu_update(int32_t, int32_t); // Update menu
void menu_display(int32_t);            // Render menu info to screen
void menu_resetmenutext(void);         // Clears all menu text cels

// Level menu functions

void menu_setlevel(int32_t, int32_t); // Select a level

// Game pause menu functions

void menu_updateviewpresets(int32_t, int32_t);        // Update all camera menus
void menu_updatecamerapos(int32_t, int32_t, int32_t); // Update camera positions
void menu_quitgame(int32_t, int32_t);                 // Return to main menu

// Formation control functions

int32_t menu_displayformation(int32_t); // Display / count formation on screen
void menu_formationgroup(int32_t, int32_t);      // Re-group formation
void menu_formationattack(int32_t, int32_t);     // Send formation into attack
void menu_changeformmode(int32_t, int32_t);      // Change formation control mode

// Music control functions

void menu_musicset(int32_t, int32_t); // Set music mode
void menu_soundset(int32_t, int32_t); // Set sound mode
void menu_trackset(int32_t, int32_t); // Toggle track on / off
void
menu_setmusictracks(void);    // Setup music playlist from menu track selections
void menu_displaymusic(void); // Display sound volume info
void menu_updatesoundmenu(int32_t, int32_t); // Check for adjusting sound bars
void menu_updatejukeboxmenu(int32_t);        // Check for moving tracks
void menu_displayjukebox(void);              // Display currently playing track

// NVRam gamefile access functions

void menu_loadgame(int32_t, int32_t);     // Load a previously saved game
void menu_savegamefile(int32_t, int32_t); // Gets filename & saves (If no room, goes into replace menu)
void menu_savegame(int32_t, int32_t);     // Save a game

// Configure functions

void menu_setvideo(int32_t, int32_t);      // Toggles video on / off
void menu_configdefault(int32_t, int32_t); // Sets game to standard configuration
void menu_setlanguage(int32_t, int32_t);   // Change language of game [CURRENTLY EUROPEAN ONLY]
void menu_setcontrol(int32_t, int32_t);    // Sets new control method
void menu_setupstick(int32_t, int32_t);    // Set up stick boundaries
void menu_setflightc(int32_t, int32_t);    // Set buttons for game controls
void menu_updateflightc(int32_t, int32_t); // Update setflightcontrols menu
void menu_saveflightc(int32_t, int32_t);   // Save config file & move up
int32_t menu_endflightc(int32_t); // Check for clashes in controls

// Get User Name functions

int32_t menu_getname(char *, int32_t, char *,
                     int32_t);   // Get name / filename from user
void menu_setpilotname(int32_t, int32_t); // Gets default name / cheat

// User message display

void menu_usermessage(int32_t); // Displays user interaction message

#endif
