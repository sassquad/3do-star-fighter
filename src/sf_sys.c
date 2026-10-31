/* sf_sys.c - timing, VBL and control pad shims */
#include "sf_compat.h"
#include "sf_platform.h"

void music_poll(void);
#define FRAME_MS 17 /* ~60Hz VBL */
static uint32_t vbl_deadline;

Item
GetVRAMIOReq(void)
{
  return 1;
}

Item
GetVBLIOReq(void)
{
  return 2;
}

Item
GetTimerIOReq(void)
{
  return 3;
}

void
WaitVBL(Item req, int frames)
{
  (void) req;
  plat_pad(); /* pump events */
  music_poll();
  if (frames > 0) {
    plat_sleep_ms((uint32_t) frames * FRAME_MS);
  }
  vbl_deadline = plat_ticks_ms();
}

void
WaitVBLDefer(Item req, int frames)
{
  (void) req;
  vbl_deadline = plat_ticks_ms() + (uint32_t) frames * FRAME_MS;
}

void
WaitIO(Item req)
{
  uint32_t now = plat_ticks_ms();
  (void) req;
  plat_pad();
  music_poll();
  if ((int32_t) (vbl_deadline - now) > 0) {
    plat_sleep_ms(vbl_deadline - now);
  }
}

int
GetControlPad(int n, int wait, ControlPadEventData *d)
{
  (void) n;
  (void) wait;
  d->cped_ButtonBits = plat_pad();
  return 0;
}
