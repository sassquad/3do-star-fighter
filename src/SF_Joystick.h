/* File : Joystick reader header file (SDL port: the 3DO event-broker thread is
 * gone) */
#ifndef __SF_JOYSTICK
#define __SF_JOYSTICK
#include "sf_compat.h"

#define JOYSTICK_MAXJOYSTICKS 16

/* ProStick button bits (values are private to this port) */
#define StickUp 0x0001
#define StickDown 0x0002
#define StickLeft 0x0004
#define StickRight 0x0008
#define StickFire 0x0010
#define StickA 0x0020
#define StickB 0x0040
#define StickC 0x0080
#define StickLeftShift 0x0100
#define StickRightShift 0x0200
#define StickPlay 0x0400
#define StickStop 0x0800

/* minimal replacement for the 3DO event-broker stick calibration data */
typedef struct BS_StickData {
  int16_t minx, maxx, miny, maxy, minz, maxz;
} BS_StickData;

#define BS_SetStickMinX(s, v) ((s)->minx = (int16_t) (v))
#define BS_SetStickMaxX(s, v) ((s)->maxx = (int16_t) (v))
#define BS_SetStickMinY(s, v) ((s)->miny = (int16_t) (v))
#define BS_SetStickMaxY(s, v) ((s)->maxy = (int16_t) (v))
#define BS_SetStickMinZ(s, v) ((s)->minz = (int16_t) (v))
#define BS_SetStickMaxZ(s, v) ((s)->maxz = (int16_t) (v))
#define BS_GetStickMinX(s) ((s)->minx)
#define BS_GetStickMaxX(s) ((s)->maxx)
#define BS_GetStickMinY(s) ((s)->miny)
#define BS_GetStickMaxY(s) ((s)->maxy)
#define BS_GetStickMinZ(s) ((s)->minz)
#define BS_GetStickMaxZ(s) ((s)->maxz)
extern BS_StickData joystick;

typedef struct stick_struct {
  int32_t x, y, z, b;
} stick_struct;

int32_t joystick_initialise(void);
void joystick_terminate(void);
void joystick_poll(void);
int32_t
joystick_registerports(void); /* refresh pro_stick from the platform layer */

extern stick_struct pro_stick;
#endif
