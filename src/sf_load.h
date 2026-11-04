#ifndef SF_LOAD_H
#define SF_LOAD_H
#include "sf_compat.h"
/* Root directory of the game's SF_Resources tree (with trailing '/') */
extern char sf_resource_root[512];
#define RESOURCES_ROOT                                                         \
  "" /* all game paths are relative to sf_resource_root, applied by the loader \
      */

/* Open a path relative to the resource root, matching names case-insensitively.
   Returns NULL if the file does not exist. */
FILE *sf_fopen_resource(const char *relpath, const char *mode);
int sf_resource_exists(const char *relpath);

/* Raw load (no byte swapping): for byte maps, strings, tables of bytes. */
void *load_fileat(void *buffer, const char *fmt, ...);
/* Load and convert big-endian 32-bit words to host words ("word-swapped"
 * model). */
void *load_fileat_words(void *buffer, const char *fmt, ...);
/* Load the 320x240 image "<relpath>.png" into a linear host-order RGB555
   buffer (NULL if missing or not 320x240). */
void *load_image_png(void *buffer, const char *relpath);
/* strip the resource root prefix from a path built with RESOURCES_ROOT */
const char *sf_rel(const char *p);
#endif
