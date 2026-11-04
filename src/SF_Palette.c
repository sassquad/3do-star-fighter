#include "sf_compat.h"
//	File : SF_Palette
//	Palette changing utilities

#include "SF_Palette.h"

/**************************************/

void
palette_set(int32_t p_red, int32_t p_green, int32_t p_blue)

// Purpose : Sets the screens colour palette
// Accepts : Colour component scaling values (0 (off) to 255 (max brightness))
// Returns : nothing

{

  /* On the 3DO this scaled the screens' colour lookup tables (CLUTs).  The
     port has never emulated display CLUTs (the shim was a no-op), so the
     goldens are recorded without it; kept as a no-op. */
  (void) p_red;
  (void) p_green;
  (void) p_blue;
}
