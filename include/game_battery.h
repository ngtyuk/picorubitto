#ifndef GAME_BATTERY_H
#define GAME_BATTERY_H
#include <stdbool.h>
#include <stdint.h>

/* 1:2 divider and nominal 3.3V reference, sampled every 20ms. */
struct game_battery_state { bool low; uint16_t count; };
static inline void game_battery_sample(struct game_battery_state *state, uint16_t raw)
{
  bool crossed = state->low ? raw >= 1489u : raw <= 1365u;
  if (!crossed) state->count = 0;
  else if (++state->count >= 150u) {
    state->low = !state->low;
    state->count = 0;
  }
}
/* Solid when healthy; 500 ms on / 500 ms off while low. */
static inline bool game_battery_led_on(bool low, uint32_t now_us)
{
  return !low || (now_us / 500000u) % 2u == 0u;
}
#endif
