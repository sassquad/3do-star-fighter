/* sf_video.c - screens and frame pacing (see sf_video.h) */
#include "sf_video.h"
#include "sf_platform.h"

void music_poll(void);

#define FRAME_MS 17 /* ~60 Hz VBL */

static uint16_t *g_fb[2];
static int g_back; /* buffer being drawn into */

/* Display brightness, 0..256. On the 3DO the fades work on the screens'
   colour tables, which keep their value: after a fade to black the next screen
   shown is still black until the fade up. Presenting at full brightness here
   flashed each new picture for a frame before it faded in (Fednet logo, menu
   background, mission pyramid...). */
static int g_display_br = 256;

static uint32_t vbl_deadline;

int
video_open(void)
{
  int i;
  for (i = 0; i < 2; i++) {
    if (!g_fb[i]) {
      g_fb[i] = (uint16_t *) calloc(SF_SCREEN_W * SF_SCREEN_H, 2);
    }
  }
  g_back = 0;
  return plat_init();
}

void
video_close(void)
{
  plat_shutdown();
}

uint16_t *
video_buffer(int n)
{
  return g_fb[n & 1];
}

int
video_back(void)
{
  return g_back;
}

void
video_show(int n)
{
  plat_present(g_fb[n & 1], g_display_br);
}

void
video_flip(void)
{
  video_show(g_back);
  g_back = 1 - g_back;
}

void
video_fill(int n, uint16_t colour)
{
  uint16_t *d = g_fb[n & 1];
  int i;
  for (i = 0; i < SF_SCREEN_W * SF_SCREEN_H; i++) {
    d[i] = colour;
  }
}

void
video_copy_in(int n, const uint16_t *image)
{
  memcpy(g_fb[n & 1], image, SF_SCREEN_W * SF_SCREEN_H * 2);
}

void
video_fade_out(int frames)
{
  int i;
  uint16_t *fb = g_fb[1 - g_back];
  for (i = 1; i <= frames; i++) {
    plat_present(fb, 256 - 256 * i / frames);
    frame_wait(1);
  }
  g_display_br = 0; /* stays black until video_fade_in */
}

void
video_fade_in(int frames)
{
  int i;
  uint16_t *fb = g_fb[1 - g_back];
  for (i = 1; i <= frames; i++) {
    plat_present(fb, 256 * i / frames);
    frame_wait(1);
  }
  g_display_br = 256;
}

void
frame_wait(int ticks)
{
  plat_pad(); /* pump events */
  music_poll();
  if (ticks > 0) {
    plat_sleep_ms((uint32_t) ticks * FRAME_MS);
  }
  vbl_deadline = plat_ticks_ms();
}

void
frame_defer(int ticks)
{
  vbl_deadline = plat_ticks_ms() + (uint32_t) ticks * FRAME_MS;
}

void
frame_sync(void)
{
  uint32_t now = plat_ticks_ms();
  plat_pad();
  music_poll();
  if ((int32_t) (vbl_deadline - now) > 0) {
    plat_sleep_ms(vbl_deadline - now);
  }
}
