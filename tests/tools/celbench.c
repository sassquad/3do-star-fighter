/* celbench - replay cel lists recorded with SF_CELTRACE (see src/sf_celtrace.h).
 *
 *   celbench [-n repeats] [--ref] trace.bin
 *
 * 1. Checks that the renderer (src/sf_cel.c) reproduces every recorded
 *    framebuffer exactly; a trace recorded before a renderer change is
 *    therefore a regression test for it.  With --ref the frozen reference
 *    renderer (tests/ref/sf_cel_ref.c) is checked instead.
 * 2. Times 'repeats' passes over all recorded lists (default 20) and prints
 *    milliseconds per pass and per recorded frame: a quiet, repeatable
 *    measurement of the part of the game that takes ~95 % of the time.
 * Exit status 1 if any framebuffer differs. */
#include "sf_compat.h"
#include "sf_video.h"
#include "sf_celtrace.h"
#include "../ref/sf_cel_ref.c"
#include <time.h>

#define NPIX (SF_SCREEN_W * SF_SCREEN_H)

typedef struct {
  uint32_t frame, fbidx, count;
  uint16_t *before[2], *after;
  CCB *ccbs;
  uint32_t **data;
  uint32_t (*plut)[16];
} List;

static void
die(const char *m)
{
  fprintf(stderr, "celbench: %s\n", m);
  exit(2);
}

static void
rd(void *p, size_t n, FILE *f)
{
  if (fread(p, 1, n, f) != n) {
    die("truncated trace");
  }
}

static uint32_t
rd32(FILE *f)
{
  uint32_t v;
  rd(&v, 4, f);
  return v;
}

static double
now(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int
main(int argc, char **argv)
{
  List *lists = NULL;
  size_t nlists = 0, cap = 0, i;
  int repeats = 20, use_ref = 0, a, r, bad = 0;
  const char *path = NULL;
  char magic[8];
  FILE *f;
  uint32_t frames = 0, lastframe = 0xffffffffu;
  double t0, t1;
  for (a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "-n") && a + 1 < argc) {
      repeats = atoi(argv[++a]);
    } else if (!strcmp(argv[a], "--ref")) {
      use_ref = 1;
    } else {
      path = argv[a];
    }
  }
  if (!path) {
    die("usage: celbench [-n repeats] [--ref] trace.bin");
  }
  f = fopen(path, "rb");
  if (!f) {
    die("cannot open trace");
  }
  rd(magic, 8, f);
  if (memcmp(magic, CELTRACE_MAGIC, 8)) {
    die("not a cel trace");
  }
  for (;;) {
    uint32_t tag, k;
    List *L;
    if (fread(&tag, 4, 1, f) != 1) {
      break;
    }
    if (tag != CELTRACE_LIST) {
      die("bad record");
    }
    if (nlists == cap) {
      cap = cap ? cap * 2 : 64;
      lists = realloc(lists, cap * sizeof *lists);
    }
    L = &lists[nlists++];
    L->frame = rd32(f);
    L->fbidx = rd32(f) & 1;
    if (L->frame != lastframe) {
      frames++;
      lastframe = L->frame;
    }
    for (k = 0; k < 2; k++) {
      L->before[k] = malloc(NPIX * 2);
      rd(L->before[k], NPIX * 2, f);
    }
    L->count = rd32(f);
    L->ccbs = calloc(L->count ? L->count : 1, sizeof(CCB));
    L->data = calloc(L->count ? L->count : 1, sizeof(uint32_t *));
    L->plut = calloc(L->count ? L->count : 1, sizeof *L->plut);
    for (k = 0; k < L->count; k++) {
      CCB *c = &L->ccbs[k];
      uint32_t v[14], kind, bytes;
      rd(v, 56, f);
      c->ccb_Flags = v[0];
      c->ccb_XPos = (int32_t) v[1];
      c->ccb_YPos = (int32_t) v[2];
      c->ccb_HDX = (int32_t) v[3];
      c->ccb_HDY = (int32_t) v[4];
      c->ccb_VDX = (int32_t) v[5];
      c->ccb_VDY = (int32_t) v[6];
      c->ccb_HDDX = (int32_t) v[7];
      c->ccb_HDDY = (int32_t) v[8];
      c->ccb_PIXC = v[9];
      c->ccb_PRE0 = v[10];
      c->ccb_PRE1 = v[11];
      c->ccb_Width = (int32_t) v[12];
      c->ccb_Height = (int32_t) v[13];
      kind = rd32(f);
      bytes = rd32(f);
      if (kind == 1) {
        L->data[k] = calloc(bytes / 4 + 16, 4); /* zero padding past the end */
        rd(L->data[k], bytes, f);
      }
      /* kinds 2/3 (framebuffer sources) are resolved at replay time */
      c->ccb_SourcePtr = (CelData *) (uintptr_t) kind;
      if (rd32(f)) {
        rd(L->plut[k], 64, f);
        c->ccb_PLUTPtr = L->plut[k];
      }
      c->ccb_NextPtr = (k + 1 < L->count) ? &L->ccbs[k + 1] : NULL;
    }
    if (rd32(f) != CELTRACE_DONE) {
      die("bad record end");
    }
    L->after = malloc(NPIX * 2);
    rd(L->after, NPIX * 2, f);
  }
  fclose(f);
  if (!video_open()) {
    die("video_open failed");
  }
  /* resolve sources now that the framebuffers exist */
  for (i = 0; i < nlists; i++) {
    uint32_t k;
    for (k = 0; k < lists[i].count; k++) {
      CCB *c = &lists[i].ccbs[k];
      uintptr_t kind = (uintptr_t) c->ccb_SourcePtr;
      c->ccb_SourcePtr = kind == 1   ? (CelData *) lists[i].data[k]
                         : kind == 2 ? (CelData *) video_buffer(0)
                         : kind == 3 ? (CelData *) video_buffer(1)
                                     : NULL;
    }
  }
  t0 = 0;
  for (r = -1; r < repeats; r++) {
    double s = now();
    for (i = 0; i < nlists; i++) {
      List *L = &lists[i];
      uint16_t *fb = video_buffer(L->fbidx);
      memcpy(video_buffer(0), L->before[0], NPIX * 2);
      memcpy(video_buffer(1), L->before[1], NPIX * 2);
      if (use_ref) {
        ref_draw_cels(fb, L->ccbs, video_buffer(0), video_buffer(1));
      } else {
        draw_cels(L->fbidx, L->ccbs);
      }
      if (r < 0 && memcmp(fb, L->after, NPIX * 2)) {
        int p = 0;
        while (fb[p] == L->after[p]) {
          p++;
        }
        if (bad++ < 10) {
          printf("list %zu (frame %u): framebuffer differs, first at (%d,%d) got %04x want %04x\n", i,
                 L->frame, p % SF_SCREEN_W, p / SF_SCREEN_W, fb[p], L->after[p]);
        }
      }
    }
    if (r == 0) {
      t0 = s;
    }
  }
  t1 = now();
  printf("%s: %zu lists in %u frames: %s; %.2f ms per pass, %.3f ms per frame (%d passes)\n",
         use_ref ? "reference renderer" : "renderer", nlists, frames,
         bad ? "FRAMEBUFFERS DIFFER" : "all framebuffers identical",
         repeats > 0 ? (t1 - t0) * 1000 / repeats : 0.0,
         repeats > 0 && frames ? (t1 - t0) * 1000 / repeats / frames : 0.0, repeats);
  return bad ? 1 : 0;
}
