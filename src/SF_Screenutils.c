#include "sf_compat.h"
//	File : SF_Screen_Utils
//	General screen utilities

// Includes

#include "SF_Screenutils.h"
#include "SF_ARMSky.h"
#include "SF_ARMCell.h"
#include "sf_video.h"

// Defines

#define DISPLAY_FRAMERATE 3      // Max speed of frame update
#define DISPLAY_FRAMERATESPACE 4 // Max speed of frame update for space

// Global variables

int32_t screen_swaptype;

/**************************************/

void
screen_base(int32_t updateflags)
{

  // Purpose : Optionally prints image backdrop or SPORT clears screen
  // Accepts : Update flags
  // Returns : nothing

  // WAIT FOR FRAME DELAY ?

  if (updateflags & S_WAIT) {
    frame_sync();
  }

  // DRAW BACKDROP IMAGE ?

  if (updateflags & S_IMAGE) {
    video_copy_in(video_back(), (const uint16_t *) backdrop);
  }

  if (updateflags & S_FLASH) {
    video_fill(video_back(), 0);
  }
}

/**************************************/

void
screen_update(int32_t updateflags)
{

  // Purpose : Screen display, optionally prints cel list, menu etc. & flips
  // screens Accepts : Update flags Returns : nothing

  int32_t temp_cels;

  // WAIT FOR FRAME DELAY ?

  if ((updateflags & S_WAIT) || (updateflags & S_WAITFRAME) ||
      (updateflags & S_WAITSPACE)) {
    frame_sync();
  }

  // DRAW CEL_PLOTLIST ?

  if ((updateflags & S_CEL) && (cel_quad.temp_cels > 0)) {
    temp_cels = cel_quad.temp_cels - 1;

    (*(cel_plotlist + temp_cels))->ccb_Flags |= CCB_LAST;
    draw_cels(video_back(), *cel_plotlist);
    (*(cel_plotlist + temp_cels))->ccb_Flags &= ~CCB_LAST;
  }

  // DRAW MENU ?

  if ((updateflags & S_MENU) && menu_status.hidden == 0) {
    draw_cels(video_back(), menu_status.cel_textccb);
  }

  // DRAW GAME INFO TEXT ? (If Also drawing menu, skip first 3 (Score, Weapon
  // Count & Mission Timer)

  if (updateflags & S_INFO) {
    if (updateflags & S_MENU || updateflags & S_DEMO) {
      draw_cels(video_back(),
                messages[(updateflags & S_MENU) ? 3 : 7].textcel->tc_CCB);
    } else {
      draw_cels(video_back(), messages[0].textcel->tc_CCB);
    }
  }

  // FLIP SCREEN BANKS ?

  if (updateflags & S_BANK) {
    if (screen_swaptype != 0) {
      screen_swap(screen_swaptype); // Do pre-flip screenswap ?
    }

    video_flip(); // Show the finished screen, draw into the other one

    if (screen_swaptype != 0) {
      screen_postswap(screen_swaptype); // Do post-flip screenswap ?
    }
  }

  // RESTART NEXT DELAY ?

  if (updateflags & S_WAIT) {
    frame_defer(DISPLAY_FRAMERATE);
  }

  if (updateflags & S_WAITSPACE) {
    frame_defer(DISPLAY_FRAMERATESPACE);
  }

  if (updateflags & S_WAITFRAME) {
    frame_defer(1);
  }
}

/**************************************/

void
screen_intercept(void)
{

  // Purpose : Routine to draw cels and clear list (if temp list becomes full)
  // Accepts : Nothing
  // Returns : Nothing

  int32_t temp_cels;

  // WAIT FOR DELAY

  frame_sync();

  // DRAW CURRENT (FULL) CEL LIST, RESET FOR NEXT BATCH

  if (cel_quad.temp_cels > 0) {

    temp_cels = cel_quad.temp_cels - 1;

    (*(cel_plotlist + temp_cels))->ccb_Flags |= CCB_LAST;
    draw_cels(video_back(), *cel_plotlist);
    (*(cel_plotlist + temp_cels))->ccb_Flags &= ~CCB_LAST;

    cel_quad.temp_cels = 0;
  }
}

/**************************************/

void
screen_clear(int32_t red, int32_t green, int32_t blue, int32_t screen_type)

// Purpose : Clears the screen
// Accepts : Screen, RGB colour
// Returns : Nothing

{
  uint16_t colour = (uint16_t) MakeRGB15(red, green, blue);

  if (screen_type == SCR_UPDATE) {
    video_fill(video_back(), colour);
  } else {
    video_fill(1 - video_back(), colour);
  }
}

/**************************************/

void
screen_swap(int32_t swap_type)

// Purpose : Calls relavent screenswap routine
// Accepts : Screenswap type
// Returns : Nothing

{

  switch (swap_type) {
    case 1:
      video_fade_out(20); // SCREENSWAP 1 ?
      break;
    case 2:
      screenswap_blurscreen(); // SCREENSWAP 2 ?
      break;
    case 3:
      screenswap_zoominbox(); // SCREENSWAP 3 ?
      break;
  }
}

/**************************************/

void
screen_postswap(int32_t swap_type)

// Purpose : Calls relavent screenswap routine (after banks flipped)
// Accepts : Screenswap type
// Returns : Nothing

{

  switch (swap_type) {
    case 1:
      video_fade_in(20); // SCREENSWAP 1 ?
      break;
  }

  screen_swaptype = 0; // Clear global next-swaptype flag
}

/**************************************/

void
screen_setswap(int32_t swap_type)

// Purpose : Sets next type of screen swap when banks are flipped
// Accepts : Screenswap type
// Returns : Nothing

{
  screen_swaptype = swap_type; // Set global next-swaptype flag
}

/**************************************/

void
screenswap_zoominbox(void)

// Purpose : SCREENSWAP1 - Zooms screen in a box
// Accepts : Nothing
// Returns : Nothing

{

  CCB screen_fade;
  int32_t fade_loop, fade_xloop, fade_yloop;

  screen_fade.ccb_Flags = CCB_LAST |   // This is the only cel
                          CCB_SPABS |  // Absolute SOURCEPTR
                          CCB_LDSIZE | // Load in HDX, HDY, VDX, VDY
                          CCB_LDPRS |  // Load in HDDX, HDDY
                          (1 << 24) |  // Load in PIXC word
                          CCB_YOXY |   // Set x&y position
                          CCB_ACW |    // Enable clockwise pixel rendering
                          CCB_ACCW | // Enable counter clockwise pixel rendering
                          CCB_ACE |  // Enable 2nd cel engine
                          CCB_CCBPRE | // Preamble words in CCB
                          (1 << 8) |   // Use PMode 0
                          CCB_USEAV |  // Use AV control bits
                          CCB_BGND;    // pass 000rgb as colour - no transp

  screen_fade.ccb_HDY = 0;
  screen_fade.ccb_VDX = 0;
  screen_fade.ccb_HDDX = 0;
  screen_fade.ccb_HDDY = 0;

  screen_fade.ccb_PRE0 = (119 << 6) | // 120 pixels high !
                         (1 << 4) |   // uncoded cel
                         6;           // 16bpp

  screen_fade.ccb_PRE1 = (318 << 16) | // 160 words int32_t
                         (1 << 11) |   // left / right memory format
                         319;          // 320 Pixel per row

  screen_fade.ccb_SourcePtr = (CelData *) video_buffer(video_back());
  for (fade_loop = 0; fade_loop < 170; fade_loop += 8) {

    if (fade_loop <= 160) {
      fade_xloop = fade_loop;
    } else {
      fade_xloop = 160;
    }

    fade_yloop = (int32_t) ((fade_xloop * 120) / 160);

    screen_fade.ccb_PIXC = (((int32_t) (fade_loop / 23)) << 10) | // PMV is 8
                           (3 << 8) |                             // PDV is 8
                           (1 << 7) | // 2nd source is frame buffer
                           1;         // 2DV is 2

    screen_fade.ccb_XPos = (160 - fade_xloop) << 16;
    screen_fade.ccb_YPos = (120 - fade_yloop) << 16;
    screen_fade.ccb_HDX = (fade_xloop << 20) / 160;
    screen_fade.ccb_VDY = (fade_yloop << 16) / 120;
    draw_cels(1 - video_back(), &screen_fade);
  }
}

/**************************************/

void
screenswap_blurscreen(void)

// Purpose : SCREENSWAP2 - Blurs 2 screens together
// Accepts : Nothing
// Returns : Nothing

{

  CCB screen_fade;

  screen_fade.ccb_Flags = CCB_LAST |   // This is the only cel
                          CCB_SPABS |  // Absolute SOURCEPTR
                          CCB_LDSIZE | // Load in HDX, HDY, VDX, VDY
                          CCB_LDPRS |  // Load in HDDX, HDDY
                          (1 << 24) |  // Load in PIXC word
                          CCB_YOXY |   // Set x&y position
                          CCB_ACW |    // Enable clockwise pixel rendering
                          CCB_ACCW | // Enable counter clockwise pixel rendering
                          CCB_ACE |  // Enable 2nd cel engine
                          CCB_CCBPRE | // Preamble words in CCB
                          (1 << 8) |   // Use PMode 0
                          CCB_USEAV |  // Use AV control bits
                          CCB_BGND;    // pass 000rgb as colour - no transp

  screen_fade.ccb_SourcePtr = (CelData *) video_buffer(1 - video_back());
  screen_fade.ccb_HDY = 0;
  screen_fade.ccb_VDX = 0;
  screen_fade.ccb_HDDX = 0;
  screen_fade.ccb_HDDY = 0;
  screen_fade.ccb_XPos = 0;
  screen_fade.ccb_YPos = 0;
  screen_fade.ccb_HDX = (1 << 20);
  screen_fade.ccb_VDY = (1 << 16);

  screen_fade.ccb_PRE0 = (119 << 6) |  // 120 pixels high !
                         (1 << 4) |    // uncoded cel
                         6;            // 16bpp
  screen_fade.ccb_PRE1 = (318 << 16) | // 160 words int32_t
                         (1 << 11) |   // left / right memory format
                         319;          // 320 Pixel per row

  screen_fade.ccb_PIXC = (5 << 10) | // PMV is 3
                         (3 << 8) |  // PDV is 8
                         (1 << 7) |  // 2nd source is frame buffer
                         0;          // 2DV is 2

  draw_cels(video_back(), &screen_fade);
}
