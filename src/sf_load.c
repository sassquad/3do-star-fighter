/* sf_load.c - resource file access for the SDL port */
#include "sf_load.h"
#include "sf_png.h"
#include <stdarg.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

char sf_resource_root[512] = "SF_Resources/";

void
sf_swap_words(void *buf, size_t bytes)
{
#if !SF_BIG_ENDIAN
  uint32_t *w = (uint32_t *) buf;
  size_t n = bytes / 4, i;
  for (i = 0; i < n; i++) {
    w[i] = sf_bswap32(w[i]);
  }
#else
  (void) buf;
  (void) bytes;
#endif
}

/* Resolve 'rel' under root one path component at a time, ignoring case. */
static int
resolve(const char *rel, char *out, size_t outsz)
{
  char cur[1024];
  char comp[256];
  const char *p = rel;
  size_t rl = strlen(sf_resource_root);
  if (rl >= sizeof cur) {
    return 0;
  }
  memcpy(cur, sf_resource_root, rl + 1);
  while (*p) {
    size_t n = 0;
    struct stat st;
    char trial[1024];
    while (*p == '/') {
      p++;
    }
    if (!*p) {
      break;
    }
    while (*p && *p != '/' && n < sizeof comp - 1) {
      comp[n++] = *p++;
    }
    comp[n] = 0;
    if (snprintf(trial, sizeof trial, "%s%s", cur, comp) >=
        (int) sizeof trial) {
      return 0;
    }
    if (stat(trial, &st) != 0) {
      DIR *d = opendir(cur[0] ? cur : ".");
      struct dirent *e;
      int found = 0;
      if (!d) {
        return 0;
      }
      while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, comp) == 0) {
          snprintf(trial, sizeof trial, "%s%s", cur, e->d_name);
          found = 1;
          break;
        }
      }
      closedir(d);
      if (!found) {
        return 0;
      }
    }
    if (strlen(trial) + 2 >= sizeof cur) {
      return 0;
    }
    strcpy(cur, trial);
    if (*p == '/') {
      strcat(cur, "/");
    }
  }
  if (strlen(cur) >= outsz) {
    return 0;
  }
  strcpy(out, cur);
  return 1;
}

FILE *
sf_fopen_resource(const char *relpath, const char *mode)
{
  char full[1024];
  if (!resolve(relpath, full, sizeof full)) {
    return NULL;
  }
  return fopen(full, mode);
}

int
sf_resource_exists(const char *relpath)
{
  char full[1024];
  return resolve(relpath, full, sizeof full);
}

/* The game builds paths as RESOURCES_ROOT + name; strip the root if present. */
const char *sf_rel(const char *p);

static const char *
strip_root(const char *p)
{
  size_t rl = strlen(sf_resource_root);
  if (strncmp(p, sf_resource_root, rl) == 0) {
    return p + rl;
  }
  return p;
}

static void *
load_v(void *buffer, int words, const char *fmt, va_list ap)
{
  char name[512];
  FILE *f;
  long sz;
  vsnprintf(name, sizeof name, fmt, ap);
  f = sf_fopen_resource(strip_root(name), "rb");
  if (!f) {
    fprintf(stderr, "LOAD ERROR : Can't load '%s'\n", name);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (fread(buffer, 1, (size_t) sz, f) != (size_t) sz) {
    fclose(f);
    return NULL;
  }
  fclose(f);
  if (words) {
    sf_swap_words(buffer, (size_t) sz & ~(size_t) 3);
  }
  return buffer;
}

void *
load_fileat(void *buffer, const char *fmt, ...)
{
  va_list ap;
  void *r;
  va_start(ap, fmt);
  r = load_v(buffer, 0, fmt, ap);
  va_end(ap);
  return r;
}

void *
load_fileat_words(void *buffer, const char *fmt, ...)
{
  va_list ap;
  void *r;
  va_start(ap, fmt);
  r = load_v(buffer, 1, fmt, ap);
  va_end(ap);
  return r;
}

/* Load a 320x240 backdrop image, "<relpath>.png" (8-bit RGB), into a linear
 * host-order 0RRRRRGGGGGBBBBB buffer.  The PNGs were converted from the 3DO's
 * frame-buffer dumps by storing each 5-bit channel c as c<<3 | c>>2, so v>>3
 * gives back exactly the original pixel. */
void *
load_image_png(void *buffer, const char *relpath)
{
  char name[512];
  uint16_t *dst = (uint16_t *) buffer;
  uint8_t *rgb;
  int w = 0, h = 0, i;
  snprintf(name, sizeof name, "%s.png", strip_root(relpath));
  rgb = sf_png_load(name, 3, &w, &h);
  if (!rgb) {
    fprintf(stderr, "LOAD ERROR : Can't load image '%s'\n", name);
    return NULL;
  }
  if (w != 320 || h != 240) {
    fprintf(stderr, "LOAD ERROR : image '%s' is %dx%d, not 320x240\n", name, w, h);
    sf_png_free(rgb);
    return NULL;
  }
  for (i = 0; i < 320 * 240; i++) {
    dst[i] = (uint16_t) (((rgb[3 * i] >> 3) << 10) | ((rgb[3 * i + 1] >> 3) << 5) |
                         (rgb[3 * i + 2] >> 3));
  }
  sf_png_free(rgb);
  return buffer;
}

const char *
sf_rel(const char *p)
{
  return strip_root(p);
}
