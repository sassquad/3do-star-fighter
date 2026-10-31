/* SF_Sound.c - sample (SFX) player.  Same interface as the 3DO audio-folio
 * version; now backed by the SDL mixer in sf_audio.c. */
#include "SF_Sound.h"
#include "SF_Allocation.h"
#include "sf_audio.h"
#include "sf_load.h"
#include <stdio.h>

int32_t SAMPLER_INITIALISED = 0, SAMPLES_LOADED = 0, SAMPLE_MASTERVOL = 128,
        SAMPLE_MASTERSWITCH = 1;

#define MAX_CHANNELS 6
#define MAX_SAMPLES 16

static sf_sample *sample_data[MAX_SAMPLES];
sample_info samples[MAX_SAMPLES];
attachment_info attachments[MAX_CHANNELS];

extern int32_t music_audio_started;

void
sound_loadsamples(void)
{
  sample_list sample_files[] = {
      {100, "Laser.aiff"},     {100, "Missile.aiff"},   {100, "Thud.aiff"},
      {150, "Engine.aiff"},    {100, "Beep.aiff"},      {100, "Click.aiff"},
      {100, "Explosion.aiff"}, {150, "Wind.aiff"},      {100, "Chime.aiff"},
      {100, "LockOn.aiff"},    {100, "PickUp.aiff"},    {100, "Incoming.aiff"},
      {100, "Alarm.aiff"},     {100, "BeamLaser.aiff"}, {0, NULL},
  };
  char sample_file[64];
  if (SAMPLES_LOADED) {
    return;
  }
  while (sample_files[SAMPLES_LOADED].name != NULL) {
    snprintf(sample_file, sizeof sample_file, "%s%s", SAMPLE_ROOT,
             sample_files[SAMPLES_LOADED].name);
    sample_data[SAMPLES_LOADED] = sf_sample_load(sample_file);
    samples[SAMPLES_LOADED].sample_item = SAMPLES_LOADED;
    samples[SAMPLES_LOADED].priority = sample_files[SAMPLES_LOADED].priority;
    SAMPLES_LOADED++;
  }
}

void
sound_unloadsamples(void)
{
  int32_t loop;
  sound_terminate();
  for (loop = 0; loop < SAMPLES_LOADED; loop++) {
    sf_sample_free(sample_data[loop]);
    sample_data[loop] = NULL;
  }
  SAMPLES_LOADED = 0;
}

void
sound_initialise(void)
{
  int32_t loop;
  if (SAMPLER_INITIALISED == 1) {
    return;
  }
  for (loop = 0; loop < MAX_CHANNELS; loop++) {
    attachments[loop].sample_playing = -1;
  }
  SAMPLER_INITIALISED = 1;
}

void
sound_terminate(void)
{
  int32_t loop;
  if (SAMPLER_INITIALISED == 0) {
    return;
  }
  for (loop = 0; loop < MAX_CHANNELS; loop++) {
    sf_mix_voice_stop(loop);
    attachments[loop].sample_playing = -1;
  }
  SAMPLER_INITIALISED = 0;
}

int32_t
sound_playsample(int32_t sample, int32_t pitch, int32_t volume,
                 int32_t stereo_pos)
{
  int32_t ch, free_channel = -1;
  int32_t left_volume, right_volume;
  if (SAMPLE_MASTERSWITCH == 0 || !SAMPLER_INITIALISED || sample < 0 ||
      sample >= SAMPLES_LOADED || !sample_data[sample]) {
    return -1;
  }
  for (ch = 0; ch < MAX_CHANNELS && free_channel == -1; ch++) {
    if (attachments[ch].sample_playing >= 0 && !sf_mix_voice_active(ch)) {
      attachments[ch].sample_playing = -1;
    }
    if (attachments[ch].sample_playing == -1) {
      free_channel = ch;
    }
  }
  if (free_channel == -1) {
    return -1;
  }
  attachments[free_channel].sample_playing = sample;
  left_volume = ((16384 - stereo_pos) * volume) >> 7;
  right_volume = ((16384 + stereo_pos) * volume) >> 7;
  sf_mix_voice_start(free_channel, sample_data[sample], pitch, left_volume,
                     right_volume);
  return free_channel;
}

/* channel numbers come from sound_playsample(), which returns -1 when nothing
 * was started */
#define VALID_CHANNEL(c) ((c) >= 0 && (c) < MAX_CHANNELS)

int32_t
sound_stopsample(int32_t channel_number)
{
  if (!VALID_CHANNEL(channel_number) ||
      attachments[channel_number].sample_playing == -1) {
    return -1;
  }
  sf_mix_voice_stop(channel_number);
  attachments[channel_number].sample_playing = -1;
  return -1;
}

int32_t
sound_pitchbend(int32_t channel_number, int32_t pitch_bend)
{
  if (!VALID_CHANNEL(channel_number) ||
      attachments[channel_number].sample_playing == -1) {
    return 0;
  }
  sf_mix_voice_bend(channel_number, pitch_bend);
  return 1;
}

void
sound_alter(int32_t channel_number, int32_t new_volume,
            int32_t new_stereoposition)
{
  if (!VALID_CHANNEL(channel_number) ||
      attachments[channel_number].sample_playing == -1) {
    return;
  }
  sf_mix_voice_gain(channel_number,
                    ((16384 - new_stereoposition) * new_volume) >> 7,
                    ((16384 + new_stereoposition) * new_volume) >> 7);
}

void
sound_setmastervolume(int32_t v)
{
  SAMPLE_MASTERVOL = v;
  sf_sfx_master(v);
}

void
sound_switchonoroff(int32_t s)
{
  SAMPLE_MASTERSWITCH = s;
}
