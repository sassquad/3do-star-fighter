/* sf_audio.c / SF_Sound.c: AIFF samples, the voice mixer, music streams and
 * the channel guards.  This test provides its own platform layer (all plat_*
 * functions) so that it can pull the mixer output directly. */
#include "sft.h"
#include "sf_platform.h"
#include "sf_audio.h"
#include "SF_Sound.h"
#include <dirent.h>

/* ---- stub platform: replaces plat_null.o for this binary ---- */
static plat_audio_cb cb;
static uint32_t clock_ms;
int plat_init(void) { return 1; }
void plat_shutdown(void) {}
void plat_present(const uint16_t *fb, int br) { (void) fb; (void) br; }
uint32_t plat_ticks_ms(void) { return clock_ms; }
void plat_sleep_ms(uint32_t ms) { clock_ms += ms; }
uint32_t plat_pad(void) { return 0; }
void plat_stick(int32_t *x, int32_t *y, int32_t *b) { *x = *y = 128; *b = 0; }
int plat_quit_requested(void) { return 0; }
int plat_audio_open(plat_audio_cb f) { cb = f; return 1; }
void plat_audio_close(void) { cb = NULL; }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}

#define OUT_RATE 44100
static int16_t out[OUT_RATE * 2 * 4];

static void
pull(int frames)
{
  memset(out, 0x55, sizeof out);
  cb(out, frames);
}

static long
peak(int frames, int chan)
{
  long m = 0;
  int i;
  for (i = 0; i < frames; i++) {
    long v = labs((long) out[2 * i + chan]);
    if (v > m) {
      m = v;
    }
  }
  return m;
}

TEST(test_all_samples_load)
{
  DIR *d;
  struct dirent *e;
  char dir[600];
  int n = 0;
  snprintf(dir, sizeof dir, "%sSamples", sft_resources());
  d = opendir(dir);
  REQUIRE(d);
  while ((e = readdir(d)) != NULL) {
    char rel[300];
    sf_sample *s;
    if (!strstr(e->d_name, ".aiff")) {
      continue;
    }
    snprintf(rel, sizeof rel, "Samples/%s", e->d_name);
    s = sf_sample_load(rel);
    n++;
    if (!s) {
      SFT_FAIL_("%s did not load", rel);
      continue;
    }
    if (s->frames <= 0 || s->rate < 4000 || s->rate > 48000) {
      SFT_FAIL_("%s: %d frames at %d Hz", rel, s->frames, s->rate);
    }
    if (s->looping && (s->loop_start < 0 || s->loop_start >= s->frames)) {
      SFT_FAIL_("%s: loop start %d of %d", rel, s->loop_start, s->frames);
    }
    sf_sample_free(s);
  }
  closedir(d);
  CHECK_EQ(n, 14); /* the game ships 14 sound effects */
}

TEST(test_voice_plays_for_expected_time)
{
  sf_sample *s;
  int expect, played = 0;
  sft_resources();
  s = sf_sample_load("Samples/Explosion.aiff");
  REQUIRE(s && !s->looping);
  REQUIRE(sf_audio_start() && cb);
  sf_sfx_master(128);
  sf_mix_voice_start(0, s, 60, 32768, 32768); /* pitch 60 = original speed */
  expect = (int) ((long long) s->frames * OUT_RATE / s->rate);
  while (sf_mix_voice_active(0) && played < 4 * OUT_RATE) {
    pull(441);
    played += 441;
  }
  CHECK(!sf_mix_voice_active(0));
  if (abs(played - expect) > 882) {
    SFT_FAIL_("played %d frames, expected about %d", played, expect);
  }
  pull(1000); /* silent once finished */
  CHECK_EQ(peak(1000, 0), 0);
  CHECK_EQ(peak(1000, 1), 0);
  sf_mix_voice_start(0, s, 72, 32768, 32768); /* an octave up: half the time */
  played = 0;
  while (sf_mix_voice_active(0) && played < 4 * OUT_RATE) {
    pull(441);
    played += 441;
  }
  if (abs(played - expect / 2) > 882) {
    SFT_FAIL_("an octave up played %d frames, expected about %d", played,
              expect / 2);
  }
  sf_sample_free(s);
}

TEST(test_voice_gain_and_pan)
{
  sf_sample *s;
  long l, r;
  sft_resources();
  s = sf_sample_load("Samples/Laser.aiff");
  REQUIRE(s);
  REQUIRE(sf_audio_start() && cb);
  sf_sfx_master(128);
  sf_mix_voice_start(1, s, 60, 32768, 0); /* hard left */
  pull(4000);
  l = peak(4000, 0);
  r = peak(4000, 1);
  CHECK(l > 1000);
  CHECK_EQ(r, 0);
  sf_mix_voice_gain(1, 0, 32768); /* move to the right */
  pull(4000);
  CHECK_EQ(peak(4000, 0), 0);
  CHECK(peak(4000, 1) > 1000);
  sf_mix_voice_stop(1);
  pull(1000);
  CHECK_EQ(peak(1000, 1), 0);
  sf_sfx_master(0); /* master volume 0 silences effects */
  sf_mix_voice_start(1, s, 60, 32768, 32768);
  pull(4000);
  CHECK_EQ(peak(4000, 0), 0);
  sf_mix_voice_stop(1);
  sf_sfx_master(128);
  sf_sample_free(s);
}

/* a negative pitch bend (engine Doppler as a ship pulls away) must not run
   the voice backwards off the start of its sample: run under ASan */
TEST(test_negative_bend_stays_in_sample)
{
  sf_sample *s;
  sft_resources();
  s = sf_sample_load("Samples/Laser.aiff");
  REQUIRE(s);
  REQUIRE(sf_audio_start() && cb);
  sf_mix_voice_start(2, s, 60, 32768, 32768);
  pull(100);
  sf_mix_voice_bend(2, -3 * 1024);
  pull(4000); /* used to read s->data[-1], [-2]... */
  CHECK(sf_mix_voice_active(2)); /* held, not ended or wrapped */
  sf_mix_voice_bend(2, 65536);
  pull(1000);
  CHECK(peak(1000, 0) > 0); /* and it carries on once the bend is positive */
  sf_mix_voice_stop(2);
  sf_sample_free(s);
}

TEST(test_music_stream)
{
  sft_resources();
  REQUIRE(sf_audio_start() && cb);
  REQUIRE(sf_stream_open("Music/Star1.aifc"));
  sf_stream_volume(255);
  CHECK(sf_stream_playing());
  pull(OUT_RATE);
  CHECK(peak(OUT_RATE, 0) > 500);
  CHECK(peak(OUT_RATE, 1) > 500);
  sf_stream_pause(1);
  CHECK(sf_stream_paused());
  pull(4000);
  CHECK_EQ(peak(4000, 0), 0);
  sf_stream_pause(0);
  sf_stream_volume(0);
  pull(4000);
  CHECK_EQ(peak(4000, 0), 0);
  sf_stream_close();
  CHECK(!sf_stream_playing());
  CHECK(!sf_stream_open("Music/NoSuchTrack.aifc"));
}

/* Regression (PORTING_NOTES §9): sound_playsample returns -1 when nothing
   starts and callers pass that channel back. */
TEST(test_sound_channel_guards)
{
  sound_alter(-1, 100, 0);
  sound_alter(1000, 100, 0);
  CHECK_EQ(sound_stopsample(-1), -1);
  CHECK_EQ(sound_stopsample(1000), -1);
  CHECK_EQ(sound_pitchbend(-1, 65536), 0);
  CHECK_EQ(sound_pitchbend(1000, 65536), 0);
  CHECK_EQ(sound_playsample(-1, 60, 100, 0), -1);
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_all_samples_load);
  RUN(test_voice_plays_for_expected_time);
  RUN(test_voice_gain_and_pan);
  RUN(test_negative_bend_stays_in_sample);
  RUN(test_music_stream);
  RUN(test_sound_channel_guards);
  return SFT_END();
}
