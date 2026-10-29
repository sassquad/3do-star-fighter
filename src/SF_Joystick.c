/* SF_Joystick.c - proportional joystick state.
 * The original ran an event-broker thread for a ProStick; this port keeps the same
 * 'pro_stick' structure but fills it from the platform layer on demand. */
#include "SF_Joystick.h"
#include "sf_platform.h"

BS_StickData joystick;
stick_struct pro_stick = { 128, 128, 128, 0 };

int32_t joystick_initialise (void) { return 1; }
void joystick_terminate (void) {}
void joystick_poll (void) { plat_stick(&pro_stick.x, &pro_stick.y, &pro_stick.b); pro_stick.z = 128; }

int32_t joystick_registerports (void) { return 0; }   /* no ProStick: keyboard/gamepad act as the control pad */
