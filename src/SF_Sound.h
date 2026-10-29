/* File : Sound header file */

#ifndef __SF_SOUND
#define __SF_SOUND

// Includes

#include "sf_compat.h"

// Structure definitions

// DEFINITION FOR SAMPLE LOAD IN LIST

typedef struct sample_list	{
							int32_t	priority;
							char	*name;
							}	sample_list;

// DEFINITION FOR SAMPLE INFORMATION

typedef struct sample_info	{
							Item	sample_item;
							int32_t	priority;
							}	sample_info;

// DEFINITION FOR ATTACHMENT INFORMATION

typedef struct attachment_info	{
								Item	attachment_item;
								int32_t	sample_playing;
								}	attachment_info;

// Definitions for update flags

#define UPDATE_PITCH	1
#define	UPDATE_VOLUME	2

// Definitions for Sounds

#define SOUND_LASER		0
#define SOUND_MISSILE	1
#define SOUND_THUD		2
#define	SOUND_ENGINE	3
#define	SOUND_BEEP		4
#define	SOUND_CLICK		5
#define SOUND_EXPLOSION	6
#define SOUND_WIND		7
#define	SOUND_CHIME		8
#define	SOUND_LOCKON	9
#define	SOUND_PICKUP	10
#define	SOUND_INCOMING	11
#define	SOUND_ALARM		12
#define	SOUND_BEAMLASER	13

// Function Prototypes

void	sound_loadsamples (void);						// Load in game samples & setup mixer [START OF GAME]
void	sound_unloadsamples (void);						// Deletes all samples & kills mixer [END OF GAME]

void	sound_initialise (void);						// Makes sample player active [MAKE SFX READY]
void	sound_terminate (void);							// Makes sample player inactive [SUSPEND SFX TO FREE DSP MEMORY]

int32_t	sound_playsample (int32_t, int32_t, int32_t, int32_t);		// Play a sample
int32_t	sound_stopsample (int32_t);						// Stop a sample on specified channel
int32_t	sound_pitchbend (int32_t, int32_t);					// Set pitch bend for a channel
void	sound_alter (int32_t, int32_t, int32_t);					// Set new volume & stereo pos for a sample

void	sound_setmastervolume (int32_t);					// Set new master volume setting
void	sound_switchonoroff (int32_t);						// Switch sample player on / off

#endif

