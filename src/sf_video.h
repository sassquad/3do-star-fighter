/* sf_video.h - screens and frame pacing for the port.
 *
 * Replaces the 3DO Portfolio calls the game was written against (OpenGraphics,
 * ScreenContext/Item handles, DisplayScreen, CopyVRAMPages, SetVRAMPages,
 * FillRect, FadeToBlack/FromBlack, WaitVBL/WaitVBLDefer/WaitIO and their IO
 * requests).  There are two 320x240 frame buffers of 0RRRRRGGGGGBBBBB host
 * order pixels: the game draws into the back buffer while the other one is on
 * screen, and video_flip() shows the back buffer and swaps them. */
#ifndef SF_VIDEO_H
#define SF_VIDEO_H
#include "sf_compat.h"

int video_open(void);  /* 1 on success; also starts the platform back end */
void video_close(void);
uint16_t *video_buffer(int n);         /* frame buffer 0 or 1 */
int video_back(void);                  /* the buffer being drawn into */
void video_show(int n);                /* put buffer n on screen (current brightness) */
void video_flip(void);                 /* show the back buffer, then draw into the other one */
void video_fill(int n, uint16_t colour);
void video_copy_in(int n, const uint16_t *image); /* a whole 320x240 image */
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
