#ifndef GAME_BOOT_CHIME_H
#define GAME_BOOT_CHIME_H

#include <stdint.h>

/* A short built-in two-note chime; signed waveform around unsigned PCM 128. */
#define GAME_BOOT_CHIME_RATE 8000u
#define GAME_BOOT_CHIME_SAMPLES 1600u
#define GAME_BOOT_SCREEN_MIN_US 3000000u
static inline uint32_t game_boot_screen_remaining_ms(uint32_t elapsed_us)
{
  if (elapsed_us >= GAME_BOOT_SCREEN_MIN_US) return 0;
  return (GAME_BOOT_SCREEN_MIN_US - elapsed_us + 999u) / 1000u;
}
static inline void game_boot_chime_generate(uint8_t samples[GAME_BOOT_CHIME_SAMPLES])
{
  static const int8_t wave[32] = {
    0, 9, 18, 27, 34, 40, 44, 47, 48, 47, 44, 40, 34, 27, 18, 9,
    0, -9, -18, -27, -34, -40, -44, -47, -48, -47, -44, -40, -34, -27, -18, -9
  };
  uint32_t phase = 0;
  for (uint32_t i = 0; i < GAME_BOOT_CHIME_SAMPLES; i++) {
    uint32_t attack = i < 96u ? i : 96u;
    uint32_t remaining = GAME_BOOT_CHIME_SAMPLES - 1u - i;
    uint32_t envelope = remaining < 360u ? remaining * 96u / 360u : 96u;
    if (attack < envelope) envelope = attack;
    samples[i] = (uint8_t)(128 + wave[(phase * 32u) / GAME_BOOT_CHIME_RATE] * (int)envelope / 96);
    phase += i < 560u ? 660u : 990u;
    if (phase >= GAME_BOOT_CHIME_RATE) phase -= GAME_BOOT_CHIME_RATE;
  }
}
#endif
