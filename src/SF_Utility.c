#include "sf_compat.h"
//	File : SF_Utility
//	General game utility routines

#include "SF_Utility.h"
#include "SF_Io.h"

/**************************************/

char
decode_level(int32_t level)

// Purpose : Returns Char type for a level number
// Accepts : Level Number
// Returns : Ning Ning Ning

{
  switch (level) {
    case 0:
      return ('T');
    case 1:
      return ('E');
    case 2:
      return ('M');
    case 3:
      return ('H');
    case 4:
      return ('D');
  }
  return ('E');
}

/**************************************/

int32_t
return_mission(pyramid *pyramid_ptr)

// Purpose : Returns Actual mission number from x & y pyramid position
// Accepts : Pointer to pyramid
// Returns : Actual mission number (0 .. 14)

{
  return (pyramid_ptr->x_level + total(pyramid_ptr->y_level));
}

/**************************************/

int32_t
return_missionpos(int32_t x_pos, int32_t y_pos)

// Purpose : Returns Actual mission number from a passed x & y position
// Accepts : x and y position
// Returns : Actual mission number (0 .. 14)

{
  return (x_pos + total(y_pos));
}

/**************************************/

int32_t
total(int32_t var)

// Purpose : Returns total of given number (ie. total + (total-1) + (total-2)
// until <0) Accepts : it Returns : total of it

{

  int32_t totalc = 0;

  while (var > 0) {
    totalc += (var--);
  }

  return (totalc);
}

/**************************************/

void
delay_frames(int32_t frame_count)

// Purpose : Waits a given number of frames before returning
// Accepts : number of frames to wait
// Returns : now't

{
  while (--frame_count >= 0 && controlpad_read() == 0) {
    WaitVBL(VBLIOReq, 1);
  }
}
