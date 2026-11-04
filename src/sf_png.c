/* sf_png.c - PNG loading for the port's converted resources (backdrops, fonts).
 *
 * Uses the public-domain stb_image (src/stb_image.h, v2.30, unmodified), built
 * for PNG only. */
#include "sf_png.h"
#include "sf_load.h"

/* third-party code: keep the game's warning settings and lint out of it */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

uint8_t *
sf_png_load(const char *relpath, int channels, int *w, int *h)
{
  FILE *f = sf_fopen_resource(relpath, "rb");
  uint8_t *file, *pixels;
  long size;
  int n;
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > (1L << 28)) {
    fclose(f);
    return NULL;
  }
  file = (uint8_t *) malloc((size_t) size);
  if (!file || fread(file, 1, (size_t) size, f) != (size_t) size) {
    fclose(f);
    free(file);
    return NULL;
  }
  fclose(f);
  pixels = stbi_load_from_memory(file, (int) size, w, h, &n, channels);
  free(file);
  return pixels;
}

void
sf_png_free(uint8_t *pixels)
{
  stbi_image_free(pixels);
}
