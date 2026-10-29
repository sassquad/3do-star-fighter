#ifndef SF_CINEPAK_H
#define SF_CINEPAK_H
#include "sf_compat.h"
/* Minimal Cinepak (cvid) decoder producing 0RRRRRGGGGGBBBBB pixels.
   One context per stream: it keeps the previous frame (for skipped blocks) and the per-strip codebooks. */
typedef struct cinepak_ctx cinepak_ctx;
cinepak_ctx *cinepak_create (int width, int height);
void cinepak_destroy (cinepak_ctx *c);
/* data = Cinepak packet starting at the 'flags + 24-bit length' word. Returns 0 on success. */
int cinepak_decode (cinepak_ctx *c, const uint8_t *data, size_t len);
const uint16_t *cinepak_pixels (const cinepak_ctx *c);     /* width*height, row-major */
#endif
