#ifndef SF_AUDIO_H
#define SF_AUDIO_H
#include "sf_compat.h"

/* ---- sample (SFX) side ---- */
typedef struct sf_sample {
  int16_t *data;
  int32_t frames;
  int32_t rate;
  int32_t loop_start;
  int looping;
} sf_sample;

sf_sample *sf_sample_load(const char *path); /* AIFF 8/16-bit mono */
void sf_sample_free(sf_sample *s);
void sf_mix_voice_start(int ch, sf_sample *s, int32_t pitch_note,
                        int32_t gain_l, int32_t gain_r);
void sf_mix_voice_stop(int ch);
int sf_mix_voice_active(int ch);
void sf_mix_voice_bend(int ch, int32_t bend16);
void sf_mix_voice_gain(int ch, int32_t gain_l, int32_t gain_r);
/* ---- music / voice stream side ---- */
int sf_stream_open(const char *path); /* returns 1 on success */
void sf_stream_close(void);
int sf_stream_playing(void);
void sf_stream_pause(int paused);
void sf_stream_volume(int vol_0_255);
int sf_stream_paused(void);
/* ---- raw PCM source (video soundtrack): stereo S16, plays once from the start
 * ---- */
void sf_pcm_start(const int16_t *stereo, int32_t frames,
                  int32_t rate); /* buffer must stay valid until stop/end */
void sf_pcm_stop(void);
int sf_pcm_active(void);
/* ---- device ---- */
int sf_audio_start(void);
void sf_audio_stop(void);
void sf_sfx_master(int vol);
#endif
