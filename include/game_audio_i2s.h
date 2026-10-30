#ifndef GAME_AUDIO_I2S_H
#define GAME_AUDIO_I2S_H

#include <stdbool.h>
#include <stdint.h>

#define GAME_AUDIO_I2S_RATE 48000u
#ifndef GAME_AUDIO_I2S_WIDE_MATH
#define GAME_AUDIO_I2S_WIDE_MATH 0
#endif

/* Inputs are signed 16-bit PCM; phase is strictly below 48000. The largest
 * product is 65535 * 47999 = 3145614465, which fits uint32_t. Separate the
 * sign so truncation matches signed 64-bit division without a wide helper. */
static inline int32_t
game_audio_i2s_interpolate(int32_t current, int32_t next, uint32_t phase)
{
#if GAME_AUDIO_I2S_WIDE_MATH
  return current + (int32_t)(((int64_t)next - current) * phase / GAME_AUDIO_I2S_RATE);
#else
  int32_t delta = next - current;
  uint32_t magnitude = (uint32_t)(delta < 0 ? -delta : delta);
  int32_t offset = (int32_t)((magnitude * phase) / GAME_AUDIO_I2S_RATE);
  return current + (delta < 0 ? -offset : offset);
#endif
}

/* Fingerprint the first 4096 generated mono samples after master gain. This
 * measures digital PCM before DMA, not the signal received by the amplifier. */
#define GAME_AUDIO_I2S_PROBE_FRAMES 4096u
struct game_audio_pcm_probe {
  uint32_t frames, fnv1a, max_step;
  int32_t previous;
};

static inline void
game_audio_pcm_probe_reset(struct game_audio_pcm_probe *probe)
{
  probe->frames = probe->max_step = 0;
  probe->fnv1a = 2166136261u;
  probe->previous = 0;
}

static inline void
game_audio_pcm_probe_sample(struct game_audio_pcm_probe *probe, int16_t sample)
{
  if (probe->frames >= GAME_AUDIO_I2S_PROBE_FRAMES) return;
  uint16_t bits = (uint16_t)sample;
  probe->fnv1a = (probe->fnv1a ^ (bits & 255u)) * 16777619u;
  probe->fnv1a = (probe->fnv1a ^ (bits >> 8)) * 16777619u;
  int32_t delta = (int32_t)sample - probe->previous;
  uint32_t step = (uint32_t)(delta < 0 ? -delta : delta);
  if (probe->frames && step > probe->max_step) probe->max_step = step;
  probe->previous = sample;
  probe->frames++;
}

/* A reader supplies signed 16-bit-scale mono PCM, including silence at EOF. */
typedef bool (*game_audio_sample_reader)(int32_t *sample);
struct game_audio_resampler {
  uint32_t phase;
  int32_t current, next;
  bool current_valid, next_valid, primed;
};

static inline uint32_t
game_audio_i2s_word(struct game_audio_resampler *state, uint32_t source_rate,
                    game_audio_sample_reader read, bool *active)
{
  if (!state->primed) {
    state->current_valid = read(&state->current);
    state->next_valid = read(&state->next);
    state->primed = true;
  }
  *active = state->current_valid || state->next_valid;
  int32_t value = game_audio_i2s_interpolate(state->current, state->next, state->phase);
  uint32_t bits = (uint16_t)(int16_t)value;
  state->phase += source_rate;
  while (state->phase >= GAME_AUDIO_I2S_RATE) {
    state->phase -= GAME_AUDIO_I2S_RATE;
    state->current = state->next;
    state->current_valid = state->next_valid;
    state->next_valid = read(&state->next);
  }
  return (bits << 16) | bits;
}

#endif
