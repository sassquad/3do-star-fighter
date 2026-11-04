#include "sf_compat.h"
/* File : Music Player header file */

#ifndef __SF_MUSIC
#define __SF_MUSIC

// Definitions

#define MUSIC_PLAYLISTLENGTH 16 // Max number of tracks that can be programmed
#define MUSIC_PLAYFROMTRACK 0x1 // Music track control commands
#define MUSIC_MOVETRACK 0x2

// Function Prototypes

int32_t music_initialise(int32_t); // INITIALISE MUSIC PLAYER
void music_terminate(void);        // TERMINATE MUSIC PLAYER

// MAIN MUSIC CONTROL ROUTINES

void music_resetplaylist(void);       // Reset the playlist
void music_addtoplaylist(int32_t);    // Add a track to the playlist
void music_takefromplaylist(int32_t); // Take a track out of the play list

void music_play(
    int32_t, int32_t,
    int32_t); // Play a track (or entire track list) with optional repeat
void music_stop(void);   // Stop music from playing
void music_pause(void);  // Pause a track
void music_resume(void); // Resume a track
void music_maketime(
    void); // Wait for buffer fill (to give max time for other loading)
void music_setmastervolume(int32_t); // Set new volume for playback
int32_t music_query(void);           // Returns current status of player
void
music_playvoice(char *,
                ...); // Start streaming of voice over (stop music if necessary)

// INTERNAL CONTROL ROUTINES

void music_thread(void); // Music Playing Task
void music_command(int32_t);
void music_poll(void);
void music_trackgetnext(int32_t); // INTERNAL - Play next / previous track
void music_loadin(int32_t);       // INTERNAL - Load in music

// external variables


#endif
