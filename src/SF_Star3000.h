#include "sf_compat.h"
// File : Global.h - Global Game Declarations

#ifndef __SF_GLOBAL
#define __SF_GLOBAL

// INCLUDES

#include "SF_Menu.h"
#include "SF_Pyramid.h"
#include "SF_Celutils.h"
#include "SF_Font.h"

// DEFINITIONS

#define	DISPLAY_NORMAL		0							// Don't do any altering
#define DISPLAY_MENU		1							// Just displaying normal menu
#define DISPLAY_HISCORES	2							// Displaying hi-score table ?

// EXTERNAL VARIABLES

extern	gamefile	save_gamefile;						// Game saver
extern	pyramid		pyramids [4];						// Pyramids array
extern	menu_info	menu_status;						// Menu status
extern	menu		menus [MAX_MENUS];					// Menus
extern	TextCel		*cel_text [FONT_MAXMENU];			// Cels for Menu entries
extern	message		messages [FONT_MAXMESSAGE];			// Text cels for In-game messages
extern	alpha_char	cel_character;						// For adding characters to char map
extern	int32_t		cockpit_missiles;					// Number of missiles attacking player
extern	int32_t		cockpit_enemybig;					// Number of enemy big ships
extern	int32_t		cockpit_enemysmall;					// Number of enemy fighters attacking player
extern	int32_t		keypad_iomap [8];					// Keypad control map
			
// LOCAL FUNCTION PROTOTYPES

void game_initialise (void);							// Initialise game at start
void game_end (void);									// Terminates program, quits
void game_intro (void);									// Displays fednet logo, start video etc.
int32_t game_missionstart (int32_t);							// Displays mission brief, calls game_play
void game_play (int32_t);									// Main mission playing routine
void game_failed (void);								// Displays failure / hiscore check etc.
void game_debrief (void);								// Mission successful
void game_checkforhighscore (int32_t);						// Check for a high score
void game_demo (void);									// Run demo mission
void menuswitch_update (int32_t);							// Check for change of menu display
void menuswitch_next (int32_t nextdisplaytype);			// Switches menu modes (hiscore - menu - demo - credits)
void game_pyramidcomplete (void);						// Registers a pyramid as completed

#endif
	
