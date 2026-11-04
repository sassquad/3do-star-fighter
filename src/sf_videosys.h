/* sf_videosys.h - screens and frame pacing for the port.
 *
 * Replaces the 3DO Portfolio calls the game was written against (OpenGraphics,
 * ScreenContext/Item handles, DisplayScreen, CopyVRAMPages, SetVRAMPages,
 * FillRect, FadeToBlack/FromBlack, WaitVBL/WaitVBLDefer/WaitIO and their IO
 * requests).  There are two frame buffers of 0RRRRRGGGGGBBBBB host order
 * pixels, sf_fb_w x sf_fb_h (320x240 times the integer scale sf_scale): the
 * game draws into the back buffer while the other one is on screen, and
 * video_flip() shows the back buffer and swaps them. */
#ifndef SF_VIDEO_H
#define SF_VIDEO_H
#include "sf_compat.h"

/* Choose the integer scale (1..SF_MAX_SCALE) of the frame buffers.  Only
   before video_open allocates them (the platform's plat_init calls it, from
   config.txt or SF_SCALE); returns 0 for an invalid or late request. */
int video_set_scale(int n);
int video_open(void);  /* 1 on success; starts the platform back end, then
                          allocates the frame buffers at the chosen scale */
void video_close(void);
uint16_t *video_buffer(int n);         /* frame buffer 0 or 1 */
int video_back(void);                  /* the buffer being drawn into */
void video_show(int n);                /* put buffer n on screen (current brightness) */
void video_flip(void);                 /* show the back buffer, then draw into the other one */
void video_fill(int n, uint16_t colour);
void video_copy_in(int n, const uint16_t *image); /* a whole 320x240 image,
                                                     blown up to the scale */
/* Blow a 320x240 image up into an sf_fb_w x sf_fb_h one (each pixel becomes
   sf_scale x sf_scale); dst may not overlap src. */
void video_upscale(uint16_t *dst, const uint16_t *src);
/* Fade the displayed buffer to black / up from black over 'frames' frames.
   As on the 3DO (fades worked on the screens' colour tables) the display stays
   black after video_fade_out until video_fade_in. */
void video_fade_out(int frames);
void video_fade_in(int frames);

/* The cel engine (sf_cel.c): draw a CCB list into buffer n. */
void draw_cels(int n, CCB *list);

/* Frame pacing in 1/60 s ticks ("VBLs").  frame_wait() pumps input and music
   and sleeps; frame_defer() starts a delay that frame_sync() waits out, so a
   frame's drawing time counts towards it. */
void frame_wait(int ticks);
void frame_defer(int ticks);
void frame_sync(void);
#endif
