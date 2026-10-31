/* plat_null.c - headless back end for testing.
 *   SF_DUMP=dir        write PPM frames into dir (every SF_DUMP_EVERY presents,
 * default 1) SF_SCRIPT=file     lines: "<first_tick> <last_tick>
 * <BUTTON>[+BUTTON...]" (tick = presented frames, plus virtual 1/60 s since the
 * last present) SF_MAXFRAMES=n     exit after n presents Time is virtual:
 * plat_sleep_ms advances the clock instantly.
 */
#include "sf_platform.h"
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>

static void
on_alarm(int sig)
{
  void *bt[40];
  int n = backtrace(bt, 40);
  (void) sig;
  fprintf(stderr, "plat_null: watchdog fired, backtrace:\n");
  backtrace_symbols_fd(bt, n, 2);
  _exit(3);
}
#endif

static uint32_t vclock, frames, tick_at_present;
static const char *dumpdir;
static int dump_every = 1;
static long maxframes = -1;

typedef struct {
  uint32_t a, b, mask;
} Ev;

static Ev evs[256];
static int nev;

static uint32_t
name_to_mask(const char *n)
{
  static const struct {
    const char *n;
    uint32_t m;
  } t[] = {
      {"UP", ControlUp},        {"DOWN", ControlDown},    {"LEFT", ControlLeft},
      {"RIGHT", ControlRight},  {"A", ControlA},          {"B", ControlB},
      {"C", ControlC},          {"START", ControlStart},  {"X", ControlX},
      {"LS", ControlLeftShift}, {"RS", ControlRightShift}};

  size_t i;
  for (i = 0; i < sizeof t / sizeof *t; i++) {
    if (!strcmp(t[i].n, n)) {
      return t[i].m;
    }
  }
  return 0;
}

int
plat_init(void)
{
  const char *s;
  dumpdir = getenv("SF_DUMP");
  if ((s = getenv("SF_WATCHDOG"))) {
#ifndef _WIN32
    signal(SIGALRM, on_alarm);
    alarm((unsigned) atoi(s));
#endif
  }
  if ((s = getenv("SF_DUMP_EVERY"))) {
    dump_every = atoi(s) > 0 ? atoi(s) : 1;
  }
  if ((s = getenv("SF_MAXFRAMES"))) {
    maxframes = atol(s);
  }
  if ((s = getenv("SF_SCRIPT"))) {
    FILE *f = fopen(s, "r");
    char line[256];
    while (f && fgets(line, sizeof line, f) && nev < 256) {
      unsigned a, b;
      char names[200];
      if (sscanf(line, "%u %u %199s", &a, &b, names) == 3) {
        char *tok = strtok(names, "+");
        uint32_t m = 0;
        while (tok) {
          m |= name_to_mask(tok);
          tok = strtok(NULL, "+");
        }
        evs[nev].a = a;
        evs[nev].b = b;
        evs[nev].mask = m;
        nev++;
      }
    }
    if (f) {
      fclose(f);
    }
  }
  return 1;
}

void
plat_shutdown(void)
{
}

void
plat_present(const uint16_t *fb, int br)
{
  frames++;
  tick_at_present = vclock / 17;
  if (dumpdir && (frames % (uint32_t) dump_every) == 0) {
    char name[512];
    FILE *f;
    int i;
    snprintf(name, sizeof name, "%s/f%06u.ppm", dumpdir, frames);
    f = fopen(name, "wb");
    if (f) {
      fprintf(f, "P6\n320 240\n255\n");
      for (i = 0; i < 320 * 240; i++) {
        unsigned v = fb[i], r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        unsigned char px[3];
        px[0] = (unsigned char) ((r * 255 / 31) * br / 256);
        px[1] = (unsigned char) ((g * 255 / 31) * br / 256);
        px[2] = (unsigned char) ((b * 255 / 31) * br / 256);
        fwrite(px, 1, 3, f);
      }
      fclose(f);
    }
  }
  if (maxframes >= 0 && (long) frames >= maxframes) {
    fprintf(stderr, "plat_null: max frames reached\n");
    exit(0);
  }
}

uint32_t
plat_ticks_ms(void)
{
  return vclock;
}

void
plat_sleep_ms(uint32_t ms)
{
  vclock += ms;
}

uint32_t
plat_pad(void)
{
  uint32_t m = 0,
           tick =
               frames +
               (vclock / 17 -
                tick_at_present); /* presented frames + virtual time since the
                                     last present, so scripted input still
                                     advances on screens that don't present */
  int i;
  for (i = 0; i < nev; i++) {
    if (tick >= evs[i].a && tick <= evs[i].b) {
      m |= evs[i].mask;
    }
  }
  if (m && getenv("SF_TRACE_PAD")) {
    fprintf(stderr, "pad %08x at tick %u\n", m, tick);
  }
  return m;
}

void
plat_stick(int32_t *x, int32_t *y, int32_t *b)
{
  *x = 128;
  *y = 128;
  *b = 0;
}

int
plat_quit_requested(void)
{
  return 0;
}

int
plat_audio_open(plat_audio_cb cb)
{
  (void) cb;
  return 1;
}

void
plat_audio_close(void)
{
}

void
plat_audio_lock(void)
{
}

void
plat_audio_unlock(void)
{
}
