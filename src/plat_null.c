/* plat_null.c - headless back end for testing.
 * No window, no audio device; time is virtual (plat_sleep_ms advances the
 * clock instantly), so runs are deterministic and fast.  Scripted input,
 * frame dumps/hashes, state logs, audio capture, frame limit and watchdog
 * come from sf_testhooks.c (see sf_testhooks.h for the environment
 * variables).
 *
 * The audio callback is pulled synchronously as virtual time advances
 * (44100 frames per virtual second), so the mixer, music and video audio run
 * as they would on a real device and can be captured with SF_AUDIO_DUMP.
 */
#include "sf_platform.h"
#include "sf_testhooks.h"

static uint32_t vclock;
static uint64_t audio_pulled;
static plat_audio_cb audio_cb;

static void
pull_audio(void)
{
  int16_t buf[1024 * 2];
  uint64_t due = (uint64_t) vclock * 44100 / 1000;
  while (audio_cb && audio_pulled < due) {
    int n = (due - audio_pulled > 1024) ? 1024 : (int) (due - audio_pulled);
    audio_cb(buf, n);
    th_audio(buf, n);
    audio_pulled += (uint64_t) n;
  }
}

int
plat_init(void)
{
  th_init();
  return 1;
}

void
plat_shutdown(void)
{
}

void
plat_present(const uint16_t *fb, int br)
{
  th_frame(fb, br, vclock);
}

uint32_t
plat_ticks_ms(void)
{
  return vclock;
}

void
plat_sleep_ms(uint32_t ms)
{
  vclock += ms;
  pull_audio();
}

uint32_t
plat_pad(void)
{
  return th_script_pad(vclock);
}

void
plat_stick(int32_t *x, int32_t *y, int32_t *b)
{
  *x = 128;
  *y = 128;
  *b = 0;
}

int
plat_quit_requested(void)
{
  return 0;
}

int
plat_audio_open(plat_audio_cb cb)
{
  audio_cb = cb;
  audio_pulled = (uint64_t) vclock * 44100 / 1000;
  return 1;
}

void
plat_audio_close(void)
{
  audio_cb = NULL;
}

void
plat_audio_lock(void)
{
}

void
plat_audio_unlock(void)
{
}
