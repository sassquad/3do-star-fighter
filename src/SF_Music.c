/* SF_Music.c - music / voice-over player.
 * The 3DO version ran a streaming task driven by signals.  Here the same
 * playlist state machine runs in the main thread; music_poll() (called from the
 * frame-wait shims) advances to the next track when the current stream ends.
 * Decoding happens in the audio callback. */
#include "SF_Music.h"
#include "SF_Allocation.h"
#include "sf_audio.h"
#include "sf_load.h"
#include <stdarg.h>

static char *music_tracks[] = {"Obie130.aifc",  "DeathBS.aifc", "PlanetMa.aifc",
                               "Star1.aifc",    "Starhed.aifc", "FreeFall.aifc",
                               "Floating.aifc", "Higher.aifc"};

#define PLAYER_PLAYING 0x1
#define PLAYER_STOPPED 0x2
#define PLAYER_TERMINATED 0x4

int32_t MUSIC_PLAYERTASKACTIVE = 0, MUSIC_TRACK, MUSIC_MASTERVOL = 128,
        MUSIC_TRACKSLEFT, player_status = PLAYER_STOPPED;
char MUSIC_PROGRAM[MUSIC_PLAYLISTLENGTH];
char MUSIC_PROGRAMLENGTH;
static int voice_playing;

static void
apply_volume(void)
{
  int v = MUSIC_MASTERVOL * 2;
  sf_stream_volume(v > 255 ? 255 : v);
}

int32_t
music_initialise(int32_t sampleinstrumenttoplay)
{
  (void) sampleinstrumenttoplay;
  if (MUSIC_PLAYERTASKACTIVE == 1) {
    return 1;
  }
  if (!sf_audio_start()) {
    fprintf(stderr, "music: no audio device\n");
  }
  MUSIC_PLAYERTASKACTIVE = 1;
  player_status = PLAYER_STOPPED;
  return 1;
}

void
music_terminate(void)
{
  if (MUSIC_PLAYERTASKACTIVE == 0) {
    return;
  }
  sf_stream_close();
  sf_audio_stop();
  player_status = PLAYER_TERMINATED;
  MUSIC_PLAYERTASKACTIVE = 0;
}

void
music_resetplaylist(void)
{
  MUSIC_PROGRAMLENGTH = 0;
}

void
music_addtoplaylist(int32_t t)
{
  int32_t i, found = -1;
  for (i = 0; i < MUSIC_PROGRAMLENGTH; i++) {
    if (MUSIC_PROGRAM[i] == t) {
      found = i;
    }
  }
  if (found == -1 && MUSIC_PROGRAMLENGTH < MUSIC_PLAYLISTLENGTH) {
    MUSIC_PROGRAM[(int) MUSIC_PROGRAMLENGTH++] = (char) t;
  }
}

void
music_takefromplaylist(int32_t t)
{
  int32_t found = -1, shift = 0, i;
  if (MUSIC_PROGRAMLENGTH <= 0) {
    return;
  }
  for (i = 0; i < MUSIC_PROGRAMLENGTH; i++) {
    if (MUSIC_PROGRAM[i] == t) {
      found = i;
    }
  }
  if (found == -1) {
    return;
  }
  for (i = 0; i < MUSIC_PROGRAMLENGTH; i++) {
    if (i != found) {
      MUSIC_PROGRAM[shift++] = MUSIC_PROGRAM[i];
    }
  }
  MUSIC_PROGRAMLENGTH -= 1;
  if (MUSIC_TRACK == found) {
    music_play(MUSIC_MOVETRACK, 0, MUSIC_TRACKSLEFT);
  }
  if (MUSIC_TRACK > found) {
    MUSIC_TRACK -= 1;
  }
}

void
music_loadin(int32_t play_voiceover)
{
  (void) play_voiceover;
}

static void
load_track(void)
{
  char path[160];
  snprintf(path, sizeof path, "%s%s", MUSIC_ROOT,
           music_tracks[(int) MUSIC_PROGRAM[MUSIC_TRACK]]);
  apply_volume();
  if (!sf_stream_open(sf_rel(path))) {
    fprintf(stderr, "music: couldn't open %s\n", path);
  }
}

void
music_trackgetnext(int32_t mover)
{
  if (MUSIC_PROGRAMLENGTH <= 0 || MUSIC_TRACKSLEFT == 0) {
    player_status = PLAYER_STOPPED;
    return;
  }
  if (MUSIC_TRACKSLEFT > 0) {
    MUSIC_TRACKSLEFT -= 1;
  }
  if (!(player_status & PLAYER_PLAYING)) {
    return;
  }
  MUSIC_TRACK += mover;
  while (MUSIC_TRACK < 0) {
    MUSIC_TRACK += MUSIC_PROGRAMLENGTH;
  }
  while (MUSIC_TRACK >= MUSIC_PROGRAMLENGTH) {
    MUSIC_TRACK -= MUSIC_PROGRAMLENGTH;
  }
  voice_playing = 0;
  load_track();
}

void
music_play(int32_t setormove, int32_t val, int32_t trackstoplay)
{
  if (!MUSIC_PLAYERTASKACTIVE) {
    return;
  }
  if (MUSIC_PROGRAMLENGTH > 0) {
    if (setormove == MUSIC_PLAYFROMTRACK) {
      MUSIC_TRACK = val;
    } else {
      MUSIC_TRACK += val;
    }
    MUSIC_TRACKSLEFT = trackstoplay;
    sf_stream_close();
    player_status = PLAYER_PLAYING;
    music_trackgetnext(0);
  } else {
    music_stop();
  }
}

void
music_stop(void)
{
  sf_stream_close();
  player_status = PLAYER_STOPPED;
}

void
music_pause(void)
{
  if (player_status & PLAYER_PLAYING) {
    sf_stream_pause(1);
  }
}

void
music_resume(void)
{
  if (player_status & PLAYER_PLAYING) {
    sf_stream_pause(0);
  }
}

void
music_maketime(void)
{
}

int32_t
music_query(void)
{
  if ((player_status & PLAYER_PLAYING) && MUSIC_TRACK >= 0 &&
      MUSIC_TRACK < MUSIC_PROGRAMLENGTH) {
    return MUSIC_PROGRAM[MUSIC_TRACK];
  }
  return -1;
}

void
music_setmastervolume(int32_t v)
{
  if (MUSIC_PLAYERTASKACTIVE) {
    MUSIC_MASTERVOL = v;
    apply_volume();
  }
}

void
music_playvoice(char *fmt, ...)
{
  char name[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(name, sizeof name, fmt, ap);
  va_end(ap);
  if (!MUSIC_PLAYERTASKACTIVE) {
    return;
  }
  MUSIC_TRACKSLEFT = 0;
  sf_stream_close();
  player_status = PLAYER_PLAYING;
  apply_volume();
  voice_playing = 1;
  if (!sf_stream_open(sf_rel(name))) {
    fprintf(stderr, "music: no voice file %s\n", name);
    voice_playing = 0;
    player_status = PLAYER_STOPPED;
  }
}

/* Called regularly from the main thread (replaces the player task's wait loop)
 */
void
music_poll(void)
{
  if (!MUSIC_PLAYERTASKACTIVE) {
    return;
  }
  if ((player_status & PLAYER_PLAYING) && !sf_stream_playing() &&
      !sf_stream_paused()) {
    sf_stream_close();
    music_trackgetnext(1);
  }
}

void
music_thread(void)
{
}

void
music_command(int32_t c)
{
  (void) c;
}
