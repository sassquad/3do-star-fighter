#ifndef SF_PNG_H
#define SF_PNG_H
#include <stdint.h>
/* Load a PNG from the resources (path relative to the resource root, matched
   case-insensitively) as 8-bit 'channels' (1 grey, 3 RGB) per pixel, rows top
   to bottom.  NULL if missing or not a valid PNG.  Free with sf_png_free. */
uint8_t *sf_png_load(const char *relpath, int channels, int *w, int *h);
void sf_png_free(uint8_t *pixels);
#endif
