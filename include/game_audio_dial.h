#ifndef GAME_AUDIO_DIAL_H
#define GAME_AUDIO_DIAL_H

#include <stdint.h>

/* Internal platform hooks. ADC ownership stays with the audio backend. */
void game_battery_init(void);
void game_battery_poll(void);
void game_audio_dial_init(void);
void game_audio_dial_poll(void);

/* Fixed end zones tolerate ADC / potentiometer endpoint variation. Keep the
 * browser mapping in sync. Raw ADC reporting remains the unscaled reading. */
#define GAME_AUDIO_DIAL_MUTE_RAW 128u
#define GAME_AUDIO_DIAL_FULL_RAW 3800u
static inline uint8_t game_audio_dial_map(uint16_t raw)
{
  if (raw <= GAME_AUDIO_DIAL_MUTE_RAW) return 0;
  if (raw >= GAME_AUDIO_DIAL_FULL_RAW) return 255;
  return (uint8_t)(((uint32_t)raw - GAME_AUDIO_DIAL_MUTE_RAW) * 255u /
                   (GAME_AUDIO_DIAL_FULL_RAW - GAME_AUDIO_DIAL_MUTE_RAW));
}

/* Signed PCM stays centred at zero. In particular, I2S retains 16-bit precision
 * at low gain instead of quantising the master volume back to 8-bit PCM. */
static inline int32_t game_audio_apply_volume(int32_t sample, uint8_t volume)
{
  return sample * volume / 255;
}

#ifndef GAME_AUDIO_MAX_GAIN
#define GAME_AUDIO_MAX_GAIN 100
#endif
#if GAME_AUDIO_MAX_GAIN < 0 || GAME_AUDIO_MAX_GAIN > 100
#error "GAME_AUDIO_MAX_GAIN must be from 0 to 100"
#endif
/* The dial remains normalized; the hardware limit applies after mixing. */
static inline int32_t game_audio_limit_output(int32_t sample)
{
  return sample * GAME_AUDIO_MAX_GAIN / 100;
}

#endif
