/* sf_testhooks.c: script parsing, tick counting, recording, frame limit,
 * hashes, state log and WAV capture.  The hooks read the environment once and
 * write files at exit, so each case runs in a forked child. */
#include "sft.h"
#include "sf_testhooks.h"
#include "sf_compat.h"
#include <sys/wait.h>

static char dir[600];
static uint16_t fb[320 * 240];

static void
path(char *out, size_t n, const char *name)
{
  snprintf(out, n, "%s/%s", dir, name);
}

static void
write_text(const char *name, const char *text)
{
  char p[700];
  FILE *f;
  path(p, sizeof p, name);
  f = fopen(p, "w");
  if (f) {
    fputs(text, f);
    fclose(f);
  }
}

static int
count_lines(const char *name)
{
  char p[700];
  FILE *f;
  int c, n = 0;
  path(p, sizeof p, name);
  f = fopen(p, "r");
  if (!f) {
    return -1;
  }
  while ((c = fgetc(f)) != EOF) {
    n += c == '\n';
  }
  fclose(f);
  return n;
}

static void
clear_env(void)
{
  static const char *vars[] = {"SF_SCRIPT",     "SF_MAXFRAMES", "SF_DUMP",
                               "SF_DUMP_EVERY", "SF_HASH",      "SF_STATE",
                               "SF_AUDIO_DUMP", "SF_RECORD",    "SF_WATCHDOG",
                               "SF_TRACE_PAD"};
  size_t i;
  for (i = 0; i < sizeof vars / sizeof *vars; i++) {
    unsetenv(vars[i]);
  }
}

static void
set_file_env(const char *var, const char *name)
{
  char p[700];
  path(p, sizeof p, name);
  setenv(var, p, 1);
}

/* run fn in a child; return its exit status (or 128+signal) */
static int
in_child(void (*fn)(void))
{
  pid_t pid;
  int st;
  fflush(stdout);
  pid = fork();
  if (pid == 0) {
    fn();
    exit(0);
  }
  if (pid < 0 || waitpid(pid, &st, 0) < 0) {
    return -1;
  }
  return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

/* ---- script parsing ---- */

static void
child_script_masks(void)
{
  /* before any present, tick = ms / 17 */
  th_init();
  if (th_script_pad(0) != 0) {
    exit(10);
  }
  if (th_script_pad(10 * 17) != ControlUp) {
    exit(11);
  }
  if (th_script_pad(12 * 17) != (ControlUp | ControlA | ControlB)) {
    exit(12);
  }
  if (th_script_pad(20 * 17) != (ControlStart | ControlX | ControlLeftShift |
                                 ControlRightShift | ControlC | ControlLeft |
                                 ControlRight | ControlDown)) {
    exit(13);
  }
  if (th_script_pad(21 * 17) != 0) {
    exit(14);
  }
  /* after a present at t: tick = frames + elapsed 17 ms steps since then */
  th_frame(fb, 256, 1000);
  if (th_tick(1000) != 1 || th_tick(1000 + 3 * 17) != 4) {
    exit(15);
  }
  exit(0);
}

TEST(test_script_parsing)
{
  clear_env();
  write_text("ok.script", "# comment line\n"
                          "\n"
                          "10 15 UP   # trailing comment\n"
                          "12 12 A+B\n"
                          "20 20 START+X+LS+RS+C+LEFT+RIGHT+DOWN\n");
  set_file_env("SF_SCRIPT", "ok.script");
  CHECK_EQ(in_child(child_script_masks), 0);
}

static void
child_init_only(void)
{
  th_init();
  exit(0);
}

TEST(test_script_errors_are_fatal)
{
  clear_env();
  write_text("bad1.script", "10 20 JUMP\n");
  set_file_env("SF_SCRIPT", "bad1.script");
  CHECK_EQ(in_child(child_init_only), 2);
  write_text("bad2.script", "10 UP\n");
  set_file_env("SF_SCRIPT", "bad2.script");
  CHECK_EQ(in_child(child_init_only), 2);
  write_text("bad3.script", "20 10 UP\n"); /* end before start */
  set_file_env("SF_SCRIPT", "bad3.script");
  CHECK_EQ(in_child(child_init_only), 2);
  set_file_env("SF_SCRIPT", "missing.script");
  CHECK_EQ(in_child(child_init_only), 2);
}

static void
child_many_events(void)
{
  int i;
  th_init();
  for (i = 0; i < 2000; i++) {
    if (th_script_pad((uint32_t) (i * 3) * 17) != ControlA) {
      exit(20);
    }
  }
  exit(0);
}

TEST(test_script_has_no_event_limit)
{
  char p[700];
  FILE *f;
  int i;
  clear_env();
  path(p, sizeof p, "long.script");
  f = fopen(p, "w");
  REQUIRE(f);
  for (i = 0; i < 2000; i++) { /* the old back end stopped at 256 */
    fprintf(f, "%d %d A\n", i * 3, i * 3);
  }
  fclose(f);
  setenv("SF_SCRIPT", p, 1);
  CHECK_EQ(in_child(child_many_events), 0);
}

/* ---- recording ---- */
static void
child_record(void)
{
  th_init();
  th_record_pad(0, 0);
  th_record_pad(ControlA, 5 * 17);
  th_record_pad(ControlA | ControlUp, 8 * 17);
  th_record_pad(0, 12 * 17);
  th_record_pad(ControlStart, 30 * 17);
  exit(0); /* still held at exit: flushed by the exit handler */
}

static void
child_replay(void)
{
  th_init();
  if (th_script_pad(4 * 17) != 0 || th_script_pad(5 * 17) != ControlA ||
      th_script_pad(8 * 17) != (ControlA | ControlUp) ||
      th_script_pad(11 * 17) != (ControlA | ControlUp) ||
      th_script_pad(12 * 17) != 0 || th_script_pad(30 * 17) != ControlStart) {
    exit(30);
  }
  exit(0);
}

TEST(test_record_and_replay)
{
  clear_env();
  set_file_env("SF_RECORD", "rec.script");
  CHECK_EQ(in_child(child_record), 0);
  CHECK_EQ(count_lines("rec.script"), 4); /* header + 3 intervals */
  clear_env();
  set_file_env("SF_SCRIPT", "rec.script");
  CHECK_EQ(in_child(child_replay), 0);
}

/* ---- frames: limit, hashes, state, dumps ---- */
static void
child_frames(void)
{
  int i;
  th_init();
  for (i = 0; i < 10; i++) {
    fb[0] = (uint16_t) (i < 5 ? 0 : 0x7fff); /* frames 1-5 equal, then 6-10 */
    th_frame(fb, 256, (uint32_t) i * 17);
  }
  exit(40); /* not reached: SF_MAXFRAMES=7 */
}

TEST(test_maxframes_hash_state_dump)
{
  char p[700], line[256];
  FILE *f;
  char h[8][32];
  int n = 0;
  clear_env();
  setenv("SF_MAXFRAMES", "7", 1);
  set_file_env("SF_HASH", "hash.txt");
  set_file_env("SF_STATE", "state.tsv");
  setenv("SF_DUMP", dir, 1);
  setenv("SF_DUMP_EVERY", "3", 1);
  CHECK_EQ(in_child(child_frames), 0);
  CHECK_EQ(count_lines("hash.txt"), 7);
  CHECK_EQ(count_lines("state.tsv"), 8); /* header + 7 */
  path(p, sizeof p, "hash.txt");
  f = fopen(p, "r");
  REQUIRE(f);
  while (n < 8 && fgets(line, sizeof line, f)) {
    int fr;
    if (sscanf(line, "%d %31s", &fr, h[n]) != 2 || fr != n + 1 ||
        strlen(h[n]) != 16) {
      SFT_FAIL_("bad hash line '%s'", line);
    }
    n++;
  }
  fclose(f);
  REQUIRE(n == 7);
  CHECK_STR(h[0], h[4]);       /* same picture, same hash */
  CHECK(strcmp(h[4], h[5]) != 0); /* one pixel changed */
  path(p, sizeof p, "f000003.ppm");
  CHECK(access(p, F_OK) == 0);
  path(p, sizeof p, "f000006.ppm");
  CHECK(access(p, F_OK) == 0);
  path(p, sizeof p, "f000004.ppm");
  CHECK(access(p, F_OK) != 0);
}

/* ---- audio capture ---- */
static void
child_audio(void)
{
  int16_t pcm[200];
  int i;
  th_init();
  for (i = 0; i < 200; i++) {
    pcm[i] = (int16_t) (i * 100 - 10000);
  }
  if (!th_audio_wanted()) {
    exit(50);
  }
  th_audio(pcm, 100);
  th_audio(pcm, 50);
  exit(0);
}

TEST(test_wav_capture)
{
  char p[700];
  FILE *f;
  uint8_t hdr[44];
  int16_t first;
  clear_env();
  set_file_env("SF_AUDIO_DUMP", "out.wav");
  CHECK_EQ(in_child(child_audio), 0);
  path(p, sizeof p, "out.wav");
  f = fopen(p, "rb");
  REQUIRE(f);
  REQUIRE(fread(hdr, 1, 44, f) == 44);
  CHECK(memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0);
  CHECK_EQ(hdr[22], 2);                                     /* stereo */
  CHECK_EQ(hdr[24] | hdr[25] << 8 | hdr[26] << 16, 44100); /* rate */
  CHECK_EQ(hdr[40] | hdr[41] << 8, 150 * 4);               /* data bytes */
  CHECK_EQ(hdr[4] | hdr[5] << 8, 36 + 150 * 4);
  CHECK(fread(&first, 2, 1, f) == 1);
  CHECK_EQ(first, -10000);
  fclose(f);
}

int
main(void)
{
  const char *d = sft_tempdir("hooks");
  SFT_BEGIN();
  snprintf(dir, sizeof dir, "%s", d);
  RUN(test_script_parsing);
  RUN(test_script_errors_are_fatal);
  RUN(test_script_has_no_event_limit);
  RUN(test_record_and_replay);
  RUN(test_maxframes_hash_state_dump);
  RUN(test_wav_capture);
  return SFT_END();
}
