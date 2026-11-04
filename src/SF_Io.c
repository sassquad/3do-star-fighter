#include "sf_compat.h"
//	File : SF_io
//	General utilities to read control pad etc.

#include "SF_Io.h"
#include "sf_platform.h"
#include "SF_Utility.h"
#include "SF_Video.h"
#include "SF_Joystick.h"

/**************************************/

// Global variables

int32_t keypad_iomap[7] = {
    ControlX, ControlStart,     ControlA,          ControlB,
    ControlC, ControlLeftShift, ControlRightShift,
};

int32_t io_timetovideo; // Timeout to video counter, value if -1
int32_t io_keypad = 0, io_keypad_mask = 0, io_keypad_constant = 0;

/************************************************************************************************************************************
 *													VIDEO
 *PLAY ON TIMEOUT ROUTINES
 **
 ************************************************************************************************************************************/

void
io_resetvideocounter(int32_t new_timeout)

// Purpose : Resets timeout to video
// Accepts : New number of frames (without user input) before timout video is
// displayed [ NOTE : -ve value is never do vid ] Returns : Nothing

{
  io_timetovideo = new_timeout;
}

/**************************************/

void
io_playvideo(void)

// Purpose : Plays a video if no user input registered for a given time -
// timeout counter is automatically reset by video player Accepts : nothing
// Returns : nothing

{
  if (configuration.video_on == 1) {
    video_play("StarLogo");
  }
}

/************************************************************************************************************************************
 *													CONTROL
 *PAD IO ROUTINES
 **
 ************************************************************************************************************************************/

int32_t
controlpad_read(void)

// Purpose : Immediately returns status of control pad
// Accepts : nothing
// Returns : Control pad words

{
  uint32_t buttons;

  // Read keypad

  buttons = plat_pad();

  joystick_poll();
  // If using joystick control, set controlpad buttons from joystick

  if (configuration.control_method == 1) {

    // Set button bits from joystick position

    if (pro_stick.x < 80) {
      buttons |= ControlLeft;
    }
    if (pro_stick.x > 240) {
      buttons |= ControlRight;
    }
    if (pro_stick.y < 60) {
      buttons |= ControlUp;
    }
    if (pro_stick.y > 180) {
      buttons |= ControlDown;
    }

    // Set button bits from joystick buttons

    if (pro_stick.b & StickLeft) {
      buttons |= ControlLeft;
    }
    if (pro_stick.b & StickRight) {
      buttons |= ControlRight;
    }
    if (pro_stick.b & StickUp) {
      buttons |= ControlUp;
    }
    if (pro_stick.b & StickDown) {
      buttons |= ControlDown;
    }

    // Set control pad buttons A,B,C, Shift, Start & Stop

    if (pro_stick.b & StickFire) {
      buttons |= ControlA;
    }
    if (pro_stick.b & StickA) {
      buttons |= ControlA;
    }
    if (pro_stick.b & StickB) {
      buttons |= ControlB;
    }
    if (pro_stick.b & StickC) {
      buttons |= ControlC;
    }

    if (pro_stick.b & StickLeftShift) {
      buttons |= ControlLeftShift;
    }
    if (pro_stick.b & StickRightShift) {
      buttons |= ControlRightShift;
    }

    if (pro_stick.b & StickPlay) {
      buttons |= ControlStart;
    }
    if (pro_stick.b & StickStop) {
      buttons |= ControlX;
    }
  }

  // Timeout to video ?

  if (buttons == 0) {
    if (io_timetovideo > 0) {
      if (--io_timetovideo == 0) {
        io_playvideo();
      }
    }
  } else {
    io_timetovideo = MAX_WAITBEFOREVIDEO;
  }

  // Return buttons

  return (buttons);
}

/**************************************/

int32_t
controlpad_debounce(void)

// Purpose : Returns debounced keypad presses
// Accepts : nothing
// Returns : Debounced keypad presses

{
  io_keypad_constant = controlpad_read(); // Read keypad
  io_keypad = io_keypad_constant;         // Copy into keypad
  io_keypad_mask &= io_keypad;            // Update mask
  io_keypad &= ~io_keypad_mask;           // Debounce keys
  io_keypad_mask |= io_keypad;            // Update mask again

  return (io_keypad); // Return debounced keypad value
}

/**************************************/

int32_t
controlpad_constant(void)

// Purpose : Returns last registered keypresses from controlpad_debounce - These
// reading are not debounced Accepts : nothing Returns : Last keypad presses

{
  return (io_keypad_constant); // Return dlast read keypad value
}
