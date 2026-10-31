#ifndef SF_PLATFORM_H
#define SF_PLATFORM_H
#include "sf_compat.h"
/* Platform back end: window/input/timing/audio.  Two implementations:
   plat_sdl.c (SDL2) and plat_null.c (headless, frame dumping, scripted input
   for testing). */
int plat_init(void);
void plat_shutdown(void);
void plat_present(const uint16_t *fb /*320x240 0RGB1555*/,
                  int brightness /*0..256*/);
uint32_t plat_ticks_ms(void);
void plat_sleep_ms(uint32_t ms);
uint32_t plat_pad(void); /* ControlXxx bits, also pumps events */
void plat_stick(int32_t *x, int32_t *y, int32_t *buttons); /* ProStick style */
int plat_quit_requested(void);
/* audio: callback fills 'frames' stereo S16 frames at 44100 Hz */
typedef void (*plat_audio_cb)(int16_t *out, int frames);
int plat_audio_open(plat_audio_cb cb);
void plat_audio_close(void);
void plat_audio_lock(void);
void plat_audio_unlock(void);
#endif
