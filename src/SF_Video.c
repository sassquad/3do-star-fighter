/* File : SF_Video.c - video playback
 *
 * Plays the game's 3DO stream files (SF_Resources/Video/<name>): Cinepak video
 * plus SDX2 audio.
 *
 * Stream layout (big-endian, every chunk is  tag[4] size[4] time[4] channel[4]
 * ... ): SHDR            stream header (ignored)          FILL     padding
 * (ignored) FILM 'FHDR'     film header                      FILM 'FRME'  one
 * Cinepak frame FRME body:  'FRME' duration frame_size  then the Cinepak packet
 * (flags<<24|length, w, h, strips ...) SNDS 'SHDR'     sound header: 16-bit
 * stereo 22050 Hz, SDX2 SNDS 'SSMP'     sound samples 'time' of a FRME chunk is
 * in 1/240 second ticks.
 *
 * A missing video file is simply skipped (returns 0), as is a file that cannot
 * be parsed. The player ends when the movie finishes (returns 0) or when a pad
 * button is *newly* pressed (returns 1).  Buttons already held when the movie
 * starts are ignored until released.
 */
#include "SF_Video.h"
#include "SF_Allocation.h"
#include "SF_Io.h"
#include "SF_Music.h"
#include "SF_Sound.h"
#include "SF_Screenutils.h"
#include "sf_load.h"
#include "sf_audio.h"
#include "sf_cinepak.h"
#include "sf_platform.h"

#define TICKS_PER_SECOND 240

typedef struct {
  const uint8_t *data;
  size_t len;
  uint32_t time;
} video_frame;

static uint32_t
be32(const uint8_t *p)
{
  return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint8_t *
read_video_file(const char *name, size_t *len)
{
  char path[160];
  FILE *f;
  long n;
  uint8_t *buf;
  snprintf(path, sizeof path, "%s%s", VIDEO_ROOT, name);
  f = sf_fopen_resource(path,
                        "rb"); /* name matched case-insensitively; no aliases */
  if (!f) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = (uint8_t *) malloc((size_t) n);
  if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) {
    free(buf);
    buf = NULL;
  }
  fclose(f);
  *len = (size_t) n;
  return buf;
}

/* SDX2: signed square-root delta.  Odd bytes are deltas, even bytes absolute;
 * per channel. */
static int32_t
decode_sdx2(const uint8_t *src, int32_t bytes, int16_t *out, int16_t pred[2],
            int *chan)
{
  int32_t i;
  for (i = 0; i < bytes; i++) {
    int n = (int8_t) src[i], c = *chan, p;
    if (!(n & 1)) {
      pred[c] = 0;
    }
    p = pred[c] + n * (n < 0 ? -n : n) * 2;
    if (p > 32767) {
      p = 32767;
    }
    if (p < -32768) {
      p = -32768;
    }
    pred[c] = (int16_t) p;
    out[i] = (int16_t) p;
    *chan = c ^ 1;
  }
  return bytes;
}

/* 1 if any button is down that was not already down when the video started */
static int
new_button_pressed(uint32_t *held)
{
  uint32_t now = (uint32_t) controlpad_read();
  *held &= now; /* released buttons no longer count as held */
  return (now & ~*held) != 0;
}

int32_t
video_play(char *video_file)
{
  size_t len;
  uint8_t *file;
  video_frame *frames = NULL;
  int nframes = 0, maxframes = 0, status = 0, cur = -1;
  int16_t *pcm = NULL;
  int32_t pcm_samples = 0, pcm_cap = 0;
  int16_t pred[2] = {0, 0};
  int chan = 0;
  size_t p;
  cinepak_ctx *cp;
  int vw, vh;
  uint16_t screenbuf[SF_SCREEN_W * SF_SCREEN_H];
  uint32_t t0, held;
  uint32_t pcm_rate = 22050;

  file = read_video_file(video_file, &len);
  if (!file) {
    fprintf(stderr, "video: '%s' not available, skipping\n", video_file);
    return 0;
  }

  /* ---- index the stream ---- */
  for (p = 0; p + 16 <= len;) {
    uint32_t sz = be32(file + p + 4);
    if (sz < 16 || p + sz > len) {
      break;
    }
    if (memcmp(file + p, "FILM", 4) == 0 && sz > 32 &&
        memcmp(file + p + 16, "FRME", 4) == 0) {
      if (nframes == maxframes) {
        maxframes = maxframes ? maxframes * 2 : 256;
        frames = (video_frame *) realloc(frames,
                                         (size_t) maxframes * sizeof *frames);
      }
      frames[nframes].data = file + p + 28; /* the Cinepak packet */
      frames[nframes].len = sz - 28;
      frames[nframes].time = be32(file + p + 8);
      nframes++;
    } else if (memcmp(file + p, "SNDS", 4) == 0 && sz > 24) {
      if (memcmp(file + p + 16, "SSMP", 4) == 0) {
        int32_t bytes = (int32_t) be32(file + p + 20);
        if (bytes > (int32_t) (sz - 24)) {
          bytes = (int32_t) (sz - 24);
        }
        if (pcm_samples + bytes > pcm_cap) {
          pcm_cap = (pcm_samples + bytes) * 2;
          pcm = (int16_t *) realloc(pcm, (size_t) pcm_cap * sizeof *pcm);
        }
        pcm_samples +=
            decode_sdx2(file + p + 24, bytes, pcm + pcm_samples, pred, &chan);
      } else if (memcmp(file + p + 16, "SHDR", 4) == 0 && sz >= 56) {
        uint32_t r = be32(file + p + 24 +
                          24); /* sample rate field of the sound header */
        if (r >= 8000 && r <= 48000) {
          pcm_rate = r;
        }
      }
    }
    p += sz;
  }
  if (nframes == 0) {
    fprintf(stderr, "video: '%s' has no video frames, skipping\n", video_file);
    free(frames);
    free(pcm);
    free(file);
    return 0;
  }
  /* picture size comes from the first frame's Cinepak header (width, height at
   * bytes 4..7) */
  vw = (int) (frames[0].data[4] << 8 | frames[0].data[5]);
  vh = (int) (frames[0].data[6] << 8 | frames[0].data[7]);
  if (vw < 4 || vh < 4 || vw > SF_SCREEN_W || vh > SF_SCREEN_H) {
    fprintf(stderr, "video: '%s' has unsupported size %dx%d, skipping\n",
            video_file, vw, vh);
    free(frames);
    free(pcm);
    free(file);
    return 0;
  }
  vw &= ~3;
  vh &= ~3;
  cp = cinepak_create(vw, vh);

  /* ---- original behaviour: stop music / samples while the movie runs ---- */
  music_pause();
  sound_terminate();
  screen_clear(0, 0, 0, SCR_UPDATE);
  screen_clear(0, 0, 0, SCR_DISPLAY);
  io_resetvideocounter(-1);

  memset(screenbuf, 0, sizeof screenbuf);
  held = (uint32_t) controlpad_read();
  sf_audio_start();
  if (pcm && pcm_samples >= 4) {
    sf_pcm_start(pcm, pcm_samples / 2, (int32_t) pcm_rate);
  }
  t0 = plat_ticks_ms();

  for (;;) {
    uint32_t now = plat_ticks_ms() - t0;
    int due = cur;
    while (due + 1 < nframes &&
           (uint64_t) frames[due + 1].time * 1000 / TICKS_PER_SECOND <= now) {
      due++;
    }
    if (due >= nframes - 1 && cur >= nframes - 1) {
      break; /* shown the last frame: finished */
    }
    if (due != cur) {
      int k;
      for (k = cur + 1; k <= due; k++) {
        cinepak_decode(cp, frames[k].data, frames[k].len);
      }
      cur = due;
      { /* centre the picture on the 320x240 screen */
        const uint16_t *src = cinepak_pixels(cp);
        int row, ox = (SF_SCREEN_W - vw) / 2, oy = (SF_SCREEN_H - vh) / 2;
        for (row = 0; row < vh; row++) {
          memcpy(screenbuf + (size_t) (oy + row) * SF_SCREEN_W + ox,
                 src + (size_t) row * vw, (size_t) vw * sizeof(uint16_t));
        }
        plat_present(screenbuf, 256);
      }
    }
    if (new_button_pressed(&held) || plat_quit_requested()) {
      status = 1;
      break;
    }
    plat_sleep_ms(4);
  }
  /* let the last frame (and audio tail) be seen briefly unless the user cut it
   * short */
  if (status == 0) {
    uint32_t endt = plat_ticks_ms(), limit = 400;
    while (plat_ticks_ms() - endt < limit && sf_pcm_active() &&
           !new_button_pressed(&held)) {
      plat_sleep_ms(4);
    }
  }
  sf_pcm_stop();
  cinepak_destroy(cp);
  free(frames);
  free(pcm);
  free(file);

  /* ---- restore ---- */
  video_show(1 - video_back());
  io_resetvideocounter(MAX_WAITBEFOREVIDEO);
  sound_initialise();
  music_resume();
  return status;
}
