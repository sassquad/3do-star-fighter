/* sf_testhooks.h - test and automation hooks shared by both platform back ends
 * (plat_null.c and plat_sdl.c).  Everything is driven by environment
 * variables and does nothing when they are unset:
 *
 *   SF_SCRIPT=file      scripted input, one event per line:
 *                         <first_tick> <last_tick> <BUTTON>[+BUTTON...]
 *                       BUTTON is UP DOWN LEFT RIGHT A B C START X LS RS.
 *                       '#' starts a comment.  A tick is the number of
 *                       presented frames plus elapsed 1/60 s (17 ms) since
 *                       the last present, so scripts still advance on screens
 *                       that do not present.  Unknown buttons or malformed
 *                       lines are fatal (exit 2).
 *   SF_MAXFRAMES=n      exit(0) after n presented frames
 *   SF_DUMP=dir         write frames as dir/fNNNNNN.ppm ...
 *   SF_DUMP_EVERY=n     ... every n-th presented frame (default 1)
 *   SF_HASH=file        one line per presented frame: "<frame> <fnv1a-64 hex>"
 *                       of the displayed RGB image (same pixels as SF_DUMP)
 *   SF_STATE=file       one tab-separated line per presented frame with key
 *                       game state (header line first; see sf_testhooks.c)
 *   SF_AUDIO_DUMP=file  write the mixed audio output as a 44.1 kHz stereo WAV
 *   SF_RECORD=file      record the real input (keyboard/gamepad) as an
 *                       SF_SCRIPT file that can be replayed
 *   SF_WATCHDOG=sec     print a backtrace and _exit(3) if still running
 *   SF_TRACE_PAD=1      log scripted button presses to stderr
 */
#ifndef SF_TESTHOOKS_H
#define SF_TESTHOOKS_H
#include <stdint.h>

/* Read the environment.  Called from plat_init. */
void th_init(void);
/* Called once per presented frame, before the back end shows it.  now_ms is
   the platform clock.  Exits the program when SF_MAXFRAMES is reached. */
void th_frame(const uint16_t *fb, int brightness, uint32_t now_ms);
/* Current script tick at platform time now_ms. */
uint32_t th_tick(uint32_t now_ms);
/* Scripted buttons held at now_ms (0 without SF_SCRIPT). */
uint32_t th_script_pad(uint32_t now_ms);
/* Record real input (SF_RECORD); call with the real button mask. */
void th_record_pad(uint32_t mask, uint32_t now_ms);
/* Mixed audio output (stereo S16, 44.1 kHz) for SF_AUDIO_DUMP. */
void th_audio(const int16_t *pcm, int frames);
/* Whether SF_AUDIO_DUMP is active. */
int th_audio_wanted(void);
/* Number of presented frames so far. */
uint32_t th_frames(void);
/* Optional function run before th_frame exits the program (e.g. to stop an
   audio thread before the WAV file is finalised). */
void th_set_exit_hook(void (*fn)(void));
#endif
