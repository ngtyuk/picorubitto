#include <assert.h>
#include "game_battery.h"

int main(void)
{
  struct game_battery_state state = {0};
  for (int i = 0; i < 149; i++) game_battery_sample(&state, 1365);
  assert(!state.low);
  game_battery_sample(&state, 1366); /* A recovery resets the timer. */
  for (int i = 0; i < 150; i++) game_battery_sample(&state, 1365);
  assert(state.low);
  for (int i = 0; i < 300; i++) game_battery_sample(&state, 1488);
  assert(state.low);
  for (int i = 0; i < 149; i++) game_battery_sample(&state, 1489);
  assert(state.low);
  game_battery_sample(&state, 1489);
  assert(!state.low);
  assert(game_battery_led_on(false, 0));
  assert(game_battery_led_on(false, 500000));
  assert(game_battery_led_on(true, 0));
  assert(game_battery_led_on(true, 499999));
  assert(!game_battery_led_on(true, 500000));
  assert(!game_battery_led_on(true, 999999));
  assert(game_battery_led_on(true, 1000000));
  return 0;
}
