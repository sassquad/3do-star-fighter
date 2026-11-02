/* sf_testhooks.c - test and automation hooks shared by the platform back
 * ends.  See sf_testhooks.h for the environment variables. */
#include "sf_testhooks.h"
#include "sf_platform.h"
#include "SF_Control.h"
#include "SF_Menu.h"
#include "Laser_Struct.h"
#include "Bit_Struct.h"
#include "Explosion_Struct.h"
#include "Smoke_Struct.h"
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>
#endif

extern ship_stack *players_ship;
extern menu_info menu_status;
extern int32_t players_thrust_control;
extern laser_list lasers;
extern bit_list bits;
extern explosion_list explosions;
extern smoke_list smokes;

typedef struct {
  uint32_t a, b, mask;
} Ev;

static Ev *evs;
static int nev, cap_ev;
static uint32_t frames, t_at_present;
static int have_present;
static const char *dumpdir;
static int dump_every = 1;
static long maxframes = -1;
static FILE *hashf, *statef, *recf, *wavf;
static uint32_t wav_frames;
static int trace_pad;
static void (*exit_hook)(void);

/* recorder state */
static uint32_t rec_mask, rec_start;

static const struct {
  const char *n;
  uint32_t m;
} btn[] = {{"UP", ControlUp},       {"DOWN", ControlDown},
           {"LEFT", ControlLeft},   {"RIGHT", ControlRight},
           {"A", ControlA},         {"B", ControlB},
           {"C", ControlC},         {"START", ControlStart},
           {"X", ControlX},         {"LS", ControlLeftShift},
           {"RS", ControlRightShift}};
#define NBTN ((int) (sizeof btn / sizeof *btn))

static void
fatal(const char *what, const char *detail)
{
  fprintf(stderr, "sf_testhooks: %s%s%s\n", what, detail ? ": " : "",
          detail ? detail : "");
  exit(2);
}

static FILE *
open_out(const char *var, const char *mode)
{
  const char *s = getenv(var);
  FILE *f;
  if (!s || !*s) {
    return NULL;
  }
  f = fopen(s, mode);
  if (!f) {
    fprintf(stderr, "sf_testhooks: cannot open %s=%s: %s\n", var, s,
            strerror(errno));
    exit(2);
  }
  return f;
}

#ifndef _WIN32
static void
on_alarm(int sig)
{
  void *bt[40];
  int n = backtrace(bt, 40);
  (void) sig;
  fprintf(stderr, "sf_testhooks: watchdog fired after frame %u, backtrace:\n",
          frames);
  backtrace_symbols_fd(bt, n, 2);
  _exit(3);
}
#endif

static void
load_script(const char *path)
{
  FILE *f = fopen(path, "r");
  char line[512];
  int lineno = 0;
  if (!f) {
    fatal("cannot open SF_SCRIPT", path);
  }
  while (fgets(line, sizeof line, f)) {
    char *hash = strchr(line, '#'), names[400], *tok;
    unsigned a, b;
    uint32_t m = 0;
    int k;
    lineno++;
    if (hash) {
      *hash = 0;
    }
    if (sscanf(line, " %399s", names) != 1) {
      continue; /* blank or comment */
    }
    if (sscanf(line, "%u %u %399s", &a, &b, names) != 3 || b < a) {
      fprintf(stderr, "sf_testhooks: %s:%d: bad line\n", path, lineno);
      exit(2);
    }
    for (tok = strtok(names, "+"); tok; tok = strtok(NULL, "+")) {
      for (k = 0; k < NBTN && strcmp(btn[k].n, tok); k++) {
      }
      if (k == NBTN) {
        fprintf(stderr, "sf_testhooks: %s:%d: unknown button '%s'\n", path,
                lineno, tok);
        exit(2);
      }
      m |= btn[k].m;
    }
    if (nev == cap_ev) {
      cap_ev = cap_ev ? cap_ev * 2 : 256;
      evs = (Ev *) realloc(evs, (size_t) cap_ev * sizeof *evs);
      if (!evs) {
        fatal("out of memory", NULL);
      }
    }
    evs[nev].a = a;
    evs[nev].b = b;
    evs[nev].mask = m;
    nev++;
  }
  fclose(f);
}

static void
wav_header(FILE *f, uint32_t nframes)
{
  uint32_t data = nframes * 4;
  uint8_t h[44];
#define LE32(p, v)                                                             \
  do {                                                                         \
    (p)[0] = (uint8_t) (v);                                                    \
    (p)[1] = (uint8_t) ((v) >> 8);                                             \
    (p)[2] = (uint8_t) ((v) >> 16);                                            \
    (p)[3] = (uint8_t) ((v) >> 24);                                            \
  } while (0)
  memcpy(h, "RIFF", 4);
  LE32(h + 4, 36 + data);
  memcpy(h + 8, "WAVEfmt ", 8);
  LE32(h + 16, 16u);
  h[20] = 1; /* PCM */
  h[21] = 0;
  h[22] = 2; /* stereo */
  h[23] = 0;
  LE32(h + 24, 44100u);
  LE32(h + 28, 44100u * 4);
  h[32] = 4;
  h[33] = 0;
  h[34] = 16;
  h[35] = 0;
  memcpy(h + 36, "data", 4);
  LE32(h + 40, data);
#undef LE32
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, sizeof h, f);
  fseek(f, 0, SEEK_END);
}

static void
rec_flush(uint32_t end_tick)
{
  int k, first = 1;
  if (!recf || !rec_mask) {
    return;
  }
  fprintf(recf, "%u %u ", rec_start, end_tick > rec_start ? end_tick - 1 : rec_start);
  for (k = 0; k < NBTN; k++) {
    if (rec_mask & btn[k].m) {
      fprintf(recf, "%s%s", first ? "" : "+", btn[k].n);
      first = 0;
    }
  }
  fputc('\n', recf);
}

static void
at_exit(void)
{
  if (recf) {
    rec_flush(frames);
    fclose(recf);
    recf = NULL;
  }
  if (wavf) {
    wav_header(wavf, wav_frames);
    fclose(wavf);
    wavf = NULL;
  }
  if (hashf) {
    fclose(hashf);
    hashf = NULL;
  }
  if (statef) {
    fclose(statef);
    statef = NULL;
  }
}

void
th_init(void)
{
  const char *s;
  dumpdir = getenv("SF_DUMP");
  if (dumpdir && !*dumpdir) {
    dumpdir = NULL;
  }
  if ((s = getenv("SF_DUMP_EVERY")) && atoi(s) > 0) {
    dump_every = atoi(s);
  }
  if ((s = getenv("SF_MAXFRAMES")) && *s) {
    maxframes = atol(s);
  }
  if ((s = getenv("SF_SCRIPT")) && *s) {
    load_script(s);
  }
  trace_pad = getenv("SF_TRACE_PAD") != NULL;
  hashf = open_out("SF_HASH", "w");
  statef = open_out("SF_STATE", "w");
  recf = open_out("SF_RECORD", "w");
  wavf = open_out("SF_AUDIO_DUMP", "wb");
  if (wavf) {
    wav_header(wavf, 0);
  }
  if (statef) {
    fprintf(statef, "frame\tlevel\tmenu\tstatus\tscore\tlives\tweapon"
                    "\tair_kills\tground_kills\thits\tcamera\tships\tlasers"
                    "\tbits\texplosions\tsmokes\tthrust"
                    "\tx\ty\tz\txrot\tyrot\tzrot\tspeed\tshields\tdamage\n");
  }
  if (recf) {
    fprintf(recf, "# recorded with SF_RECORD\n");
  }
  atexit(at_exit);
#ifndef _WIN32
  if ((s = getenv("SF_WATCHDOG")) && atoi(s) > 0) {
    signal(SIGALRM, on_alarm);
    alarm((unsigned) atoi(s));
  }
#endif
}

void
th_set_exit_hook(void (*fn)(void))
{
  exit_hook = fn;
}

uint32_t
th_frames(void)
{
  return frames;
}

uint32_t
th_tick(uint32_t now_ms)
{
  if (!have_present) {
    return now_ms / 17;
  }
  return frames + (now_ms / 17 - t_at_present / 17);
}

uint32_t
th_script_pad(uint32_t now_ms)
{
  uint32_t m = 0, tick;
  int i;
  if (!nev) {
    return 0;
  }
  tick = th_tick(now_ms);
  for (i = 0; i < nev; i++) {
    if (tick >= evs[i].a && tick <= evs[i].b) {
      m |= evs[i].mask;
    }
  }
  if (m && trace_pad) {
    fprintf(stderr, "pad %08x at tick %u\n", m, tick);
  }
  return m;
}

void
th_record_pad(uint32_t mask, uint32_t now_ms)
{
  uint32_t tick;
  if (!recf || mask == rec_mask) {
    return;
  }
  tick = th_tick(now_ms);
  rec_flush(tick);
  rec_mask = mask;
  rec_start = tick;
}

int
th_audio_wanted(void)
{
  return wavf != NULL;
}

void
th_audio(const int16_t *pcm, int n)
{
  int i;
  if (!wavf || n <= 0) {
    return;
  }
  for (i = 0; i < n * 2; i++) {
    uint16_t v = (uint16_t) pcm[i];
    fputc(v & 255, wavf);
    fputc(v >> 8, wavf);
  }
  wav_frames += (uint32_t) n;
}

/* active items at the start of an ARMLink list (suspended items, such as the
   beam-laser pool, are not in the chain) */
static int
count_active(const linked_list *l)
{
  const link_header *h = (const link_header *) l->start_address;
  int n = 0;
  while (h && h->status == 1 && n < 100000) {
    n++;
    h = (const link_header *) h->next_address;
  }
  return n;
}

static void
write_state(void)
{
  const ship_stack *p = players_ship;
  fprintf(statef,
          "%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d"
          "\t%d",
          frames, parameters.level, menu_status.current_menu, status.status,
          status.score, status.lives, status.weapon, status.enemy_aircount,
          status.enemy_groundcount, status.hits_taken, status.current_camera,
          count_active(&ships.info), count_active(&lasers.info),
          count_active(&bits.info), count_active(&explosions.info),
          count_active(&smokes.info), players_thrust_control);
  if (p) {
    fprintf(statef, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", p->x_pos,
            p->y_pos, p->z_pos, p->x_rot, p->y_rot, p->z_rot, p->speed,
            p->shields, p->damage);
  } else {
    fputs("\t0\t0\t0\t0\t0\t0\t0\t0\t0\n", statef);
  }
}

/* RGB as written to PPM dumps (and hashed): 5-bit channels expanded with
   *255/31, then scaled by the fade brightness. */
static void
to_rgb(const uint16_t *fb, int br, unsigned char *rgb)
{
  int i;
  for (i = 0; i < 320 * 240; i++) {
    unsigned v = fb[i], r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    rgb[3 * i + 0] = (unsigned char) ((r * 255 / 31) * (unsigned) br / 256);
    rgb[3 * i + 1] = (unsigned char) ((g * 255 / 31) * (unsigned) br / 256);
    rgb[3 * i + 2] = (unsigned char) ((b * 255 / 31) * (unsigned) br / 256);
  }
}

void
th_frame(const uint16_t *fb, int br, uint32_t now_ms)
{
  static unsigned char rgb[320 * 240 * 3];
  int want_dump;
  frames++;
  t_at_present = now_ms;
  have_present = 1;
  want_dump = dumpdir && (frames % (uint32_t) dump_every) == 0;
  if (want_dump || hashf) {
    to_rgb(fb, br, rgb);
  }
  if (want_dump) {
    char name[600];
    FILE *f;
    snprintf(name, sizeof name, "%s/f%06u.ppm", dumpdir, frames);
    f = fopen(name, "wb");
    if (f) {
      fprintf(f, "P6\n320 240\n255\n");
      fwrite(rgb, 1, sizeof rgb, f);
      fclose(f);
    }
  }
  if (hashf) {
    uint64_t h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < sizeof rgb; i++) {
      h = (h ^ rgb[i]) * 1099511628211ull;
    }
    fprintf(hashf, "%u %016" PRIx64 "\n", frames, h); /* PRIx64: msvcrt too */
  }
  if (statef) {
    write_state();
  }
  if (maxframes >= 0 && (long) frames >= maxframes) {
    fprintf(stderr, "sf: max frames reached (%u)\n", frames);
    if (exit_hook) {
      exit_hook();
    }
    exit(0);
  }
}
