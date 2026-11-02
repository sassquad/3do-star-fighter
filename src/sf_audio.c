/* sf_audio.c - mixer, AIFF/AIFC(SDX2) decoding, streaming */
#include "sf_audio.h"
#include "sf_platform.h"
#include "sf_load.h"
#include <math.h>

#define NCH 6
#define OUT_RATE 44100

static uint32_t
be32(const uint8_t *p)
{
  return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint16_t
be16(const uint8_t *p)
{
  return (uint16_t) ((p[0] << 8) | p[1]);
}

static double
ieee80(const uint8_t *p)
{
  int e = ((p[0] & 0x7f) << 8) | p[1];
  uint64_t m = ((uint64_t) be32(p + 2) << 32) | be32(p + 6);
  double v = ldexp((double) m, e - 16383 - 63);
  return (p[0] & 0x80) ? -v : v;
}

typedef struct {
  int ch, bits, sdx2;
  uint32_t frames, rate;
  long data_off, data_len;
  int32_t mark_begin, mark_end, loop_mode, loop_b_id, loop_e_id;
  int32_t mk_pos[8];
  int32_t mk_id[8];
  int nmk;
} AiffInfo;

static int
aiff_parse(FILE *f, AiffInfo *a)
{
  uint8_t h[12], ck[8];
  memset(a, 0, sizeof *a);
  if (fread(h, 1, 12, f) != 12 || memcmp(h, "FORM", 4)) {
    return 0;
  }
  while (fread(ck, 1, 8, f) == 8) {
    uint32_t sz = be32(ck + 4);
    long next = ftell(f) + (long) sz + (sz & 1);
    if (!memcmp(ck, "COMM", 4)) {
      uint8_t b[64];
      size_t n = sz < sizeof b ? sz : sizeof b;
      if (fread(b, 1, n, f) != n) {
        return 0;
      }
      a->ch = be16(b);
      a->frames = be32(b + 2);
      a->bits = be16(b + 6);
      a->rate = (uint32_t) (ieee80(b + 8) + 0.5);
      if (n >= 22 && !memcmp(h + 8, "AIFC", 4)) {
        a->sdx2 = !memcmp(b + 18, "SDX2", 4);
      }
    } else if (!memcmp(ck, "SSND", 4)) {
      a->data_off = ftell(f) + 8;
      a->data_len = (long) sz - 8;
    } else if (!memcmp(ck, "MARK", 4)) {
      uint8_t *b = (uint8_t *) malloc(sz);
      uint32_t q = 2, i, n;
      if (b && fread(b, 1, sz, f) == sz) {
        n = be16(b);
        for (i = 0; i < n && i < 8 && q + 7 <= sz; i++) {
          a->mk_id[a->nmk] = be16(b + q);
          a->mk_pos[a->nmk] = (int32_t) be32(b + q + 2);
          a->nmk++;
          q += 6;
          {
            uint32_t l = b[q] + 1;
            q += l + (l & 1);
          }
        }
      }
      free(b);
    } else if (!memcmp(ck, "INST", 4)) {
      uint8_t b[20];
      if (sz >= 20 && fread(b, 1, 20, f) == 20) {
        a->loop_mode = be16(b + 8);
        a->loop_b_id = be16(b + 10);
        a->loop_e_id = be16(b + 12);
      }
    }
    fseek(f, next, SEEK_SET);
  }
  return a->data_off != 0 && a->ch > 0;
}

static int
mk_lookup(const AiffInfo *a, int id)
{
  int i;
  for (i = 0; i < a->nmk; i++) {
    if (a->mk_id[i] == id) {
      return a->mk_pos[i];
    }
  }
  return -1;
}

sf_sample *
sf_sample_load(const char *path)
{
  FILE *f = sf_fopen_resource(path, "rb");
  AiffInfo a;
  sf_sample *s;
  uint8_t *raw;
  uint32_t i;
  if (!f) {
    fprintf(stderr, "sound: can't open %s\n", path);
    return NULL;
  }
  if (!aiff_parse(f, &a) || a.ch != 1) {
    fclose(f);
    fprintf(stderr, "sound: bad AIFF %s\n", path);
    return NULL;
  }
  raw = (uint8_t *) malloc((size_t) a.data_len);
  fseek(f, a.data_off, SEEK_SET);
  if (fread(raw, 1, (size_t) a.data_len, f) != (size_t) a.data_len) {
    free(raw);
    fclose(f);
    return NULL;
  }
  fclose(f);
  s = (sf_sample *) calloc(1, sizeof *s);
  s->rate = (int32_t) a.rate;
  s->frames = (int32_t) a.frames;
  s->data = (int16_t *) malloc(a.frames * sizeof(int16_t));
  for (i = 0; i < a.frames; i++) {
    if (a.bits == 8) {
      s->data[i] = (int16_t) ((int8_t) raw[i] << 8);
    } else {
      s->data[i] = (int16_t) be16(raw + 2 * i);
    }
  }
  free(raw);
  if (a.loop_mode == 1) {
    int b = mk_lookup(&a, a.loop_b_id);
    if (b < 0) {
      b = 0;
    }
    s->looping = 1;
    s->loop_start = b;
    {
      int e = mk_lookup(&a, a.loop_e_id);
      if (e > 0 && e < s->frames) {
        s->frames = e;
      }
    }
  }
  return s;
}

void
sf_sample_free(sf_sample *s)
{
  if (s) {
    free(s->data);
    free(s);
  }
}

/* ------------------------------ voices ------------------------------ */
typedef struct {
  sf_sample *s;
  int active;
  double pos, step, bend;
  int32_t gl, gr;
} Voice;

static Voice voice[NCH];
static int sfx_master = 128;

void
sf_sfx_master(int v)
{
  sfx_master = v;
}

void
sf_mix_voice_start(int ch, sf_sample *s, int32_t pitch, int32_t gl, int32_t gr)
{
  plat_audio_lock();
  voice[ch].s = s;
  voice[ch].pos = 0;
  voice[ch].bend = 1.0;
  voice[ch].step = (double) s->rate / OUT_RATE * pow(2.0, (pitch - 60) / 12.0);
  voice[ch].gl = gl;
  voice[ch].gr = gr;
  voice[ch].active = 1;
  plat_audio_unlock();
}

void
sf_mix_voice_stop(int ch)
{
  plat_audio_lock();
  voice[ch].active = 0;
  plat_audio_unlock();
}

int
sf_mix_voice_active(int ch)
{
  return voice[ch].active;
}

void
sf_mix_voice_bend(int ch, int32_t b16)
{
  /* The engine-noise Doppler (Sound_Control.c) can ask for a negative bend:
     9*1024 + speed terms - up to 12*1024 when a ship pulls away. Playing
     backwards ran the voice off the start of its sample (ASan, attract demo);
     a rate can't be negative, so it holds at 0 instead. */
  voice[ch].bend = b16 > 0 ? b16 / 65536.0 : 0.0;
}

void
sf_mix_voice_gain(int ch, int32_t gl, int32_t gr)
{
  voice[ch].gl = gl;
  voice[ch].gr = gr;
}

/* ------------------------------ stream ------------------------------ */
static struct {
  FILE *f;
  AiffInfo a;
  int active, paused, vol;
  long remain;
  double pos, step;
  int16_t prev[2], cur[2], pred[2];
  uint8_t buf[8192];
  long buflen, bufpos;
  int primed;
} st;

static int
stream_byte(void)
{
  if (st.bufpos >= st.buflen) {
    size_t want = sizeof st.buf;
    if ((long) want > st.remain) {
      want = (size_t) st.remain;
    }
    if (want == 0) {
      return -1;
    }
    st.buflen = (long) fread(st.buf, 1, want, st.f);
    st.bufpos = 0;
    st.remain -= st.buflen;
    if (st.buflen <= 0) {
      return -1;
    }
  }
  return st.buf[st.bufpos++];
}

static int
stream_frame(int16_t out[2])
{
  int c;
  for (c = 0; c < st.a.ch; c++) {
    int v, v2;
    if (st.a.sdx2) {
      v = stream_byte();
      if (v < 0) {
        return 0;
      }
      {
        int n = (int8_t) v;
        if (!(n & 1)) {
          st.pred[c] = 0;
        }
        {
          int p = st.pred[c] + n * abs(n) * 2;
          if (p > 32767) {
            p = 32767;
          }
          if (p < -32768) {
            p = -32768;
          }
          st.pred[c] = (int16_t) p;
        }
      }
      out[c] = st.pred[c];
    } else if (st.a.bits == 16) {
      v = stream_byte();
      v2 = stream_byte();
      if (v2 < 0) {
        return 0;
      }
      out[c] = (int16_t) ((v << 8) | v2);
    } else {
      v = stream_byte();
      if (v < 0) {
        return 0;
      }
      out[c] = (int16_t) ((int8_t) v << 8);
    }
  }
  if (st.a.ch == 1) {
    out[1] = out[0];
  }
  return 1;
}

int
sf_stream_open(const char *path)
{
  FILE *f = sf_fopen_resource(path, "rb");
  sf_stream_close();
  if (!f) {
    return 0;
  }
  plat_audio_lock();
  if (!aiff_parse(f, &st.a)) {
    fclose(f);
    plat_audio_unlock();
    return 0;
  }
  fseek(f, st.a.data_off, SEEK_SET);
  st.f = f;
  st.remain = st.a.data_len;
  st.buflen = st.bufpos = 0;
  st.pos = 0;
  st.step = (double) st.a.rate / OUT_RATE;
  st.pred[0] = st.pred[1] = 0;
  st.paused = 0;
  st.active = 1;
  st.primed = stream_frame(st.cur);
  st.prev[0] = st.cur[0];
  st.prev[1] = st.cur[1];
  if (!st.vol) {
    st.vol = 128;
  }
  plat_audio_unlock();
  return 1;
}

void
sf_stream_close(void)
{
  plat_audio_lock();
  if (st.f) {
    fclose(st.f);
  }
  st.f = NULL;
  st.active = 0;
  plat_audio_unlock();
}

int
sf_stream_playing(void)
{
  return st.active;
}

void
sf_stream_pause(int p)
{
  st.paused = p;
}

int
sf_stream_paused(void)
{
  return st.paused;
}

void
sf_stream_volume(int v)
{
  st.vol = v;
}

/* ------------------------------ raw pcm ------------------------------ */
static struct {
  const int16_t *data;
  int32_t frames;
  double pos, step;
  int active;
} pcm;

void
sf_pcm_start(const int16_t *stereo, int32_t frames, int32_t rate)
{
  plat_audio_lock();
  pcm.data = stereo;
  pcm.frames = frames;
  pcm.pos = 0;
  pcm.step = (double) rate / OUT_RATE;
  pcm.active = 1;
  plat_audio_unlock();
}

void
sf_pcm_stop(void)
{
  plat_audio_lock();
  pcm.active = 0;
  plat_audio_unlock();
}

int
sf_pcm_active(void)
{
  return pcm.active;
}

/* ------------------------------ mixer ------------------------------- */
static void
mix_cb(int16_t *out, int frames)
{
  int i, c;
  for (i = 0; i < frames; i++) {
    int32_t l = 0, r = 0;
    for (c = 0; c < NCH; c++) {
      Voice *v = &voice[c];
      if (v->active) {
        int32_t idx = (int32_t) v->pos, smp = v->s->data[idx];
        l += ((smp * v->gl) >> 15) * sfx_master >> 7;
        r += ((smp * v->gr) >> 15) * sfx_master >> 7;
        v->pos += v->step * v->bend;
        if ((int32_t) v->pos >= v->s->frames) {
          if (v->s->looping) {
            v->pos = v->s->loop_start + (v->pos - v->s->frames);
          } else {
            v->active = 0;
          }
        }
      }
    }
    if (st.active && !st.paused) {
      while (st.pos >= 1.0) {
        st.prev[0] = st.cur[0];
        st.prev[1] = st.cur[1];
        if (!stream_frame(st.cur)) {
          st.active = 0;
          break;
        }
        st.pos -= 1.0;
      }
      if (st.active) {
        double fr = st.pos;
        int32_t ml = (int32_t) (st.prev[0] + (st.cur[0] - st.prev[0]) * fr);
        int32_t mr = (int32_t) (st.prev[1] + (st.cur[1] - st.prev[1]) * fr);
        l += ml * st.vol >> 8;
        r += mr * st.vol >> 8;
        st.pos += st.step;
      }
    }
    if (pcm.active) {
      int32_t i0 = (int32_t) pcm.pos;
      if (i0 + 1 >= pcm.frames) {
        pcm.active = 0;
      } else {
        double fr = pcm.pos - i0;
        l += (int32_t) (pcm.data[2 * i0] +
                        (pcm.data[2 * i0 + 2] - pcm.data[2 * i0]) * fr);
        r += (int32_t) (pcm.data[2 * i0 + 1] +
                        (pcm.data[2 * i0 + 3] - pcm.data[2 * i0 + 1]) * fr);
        pcm.pos += pcm.step;
      }
    }
    if (l > 32767) {
      l = 32767;
    }
    if (l < -32768) {
      l = -32768;
    }
    if (r > 32767) {
      r = 32767;
    }
    if (r < -32768) {
      r = -32768;
    }
    out[2 * i] = (int16_t) l;
    out[2 * i + 1] = (int16_t) r;
  }
}

static int audio_running;

int
sf_audio_start(void)
{
  if (audio_running) {
    return 1;
  }
  audio_running = plat_audio_open(mix_cb);
  return audio_running;
}

void
sf_audio_stop(void)
{
  plat_audio_close();
  audio_running = 0;
}
