#include "game_platform.h"
#include "game_audio_dial.h"
#include "game_battery.h"

#if GAME_AUDIO_VOLUME_KNOB || GAME_BATTERY_WARNING
#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/adc.h>

#endif

#if GAME_AUDIO_VOLUME_KNOB
#define AUDIO_DIAL_GPIO 42u
#define AUDIO_DIAL_ADC 2u
#define AUDIO_DIAL_INTERVAL_US 20000u

static volatile bool dial_ready;
static volatile uint16_t dial_raw;
static volatile uint8_t dial_volume = 255;
static uint32_t dial_filtered_q8;
static uint32_t dial_polled_at;

static uint16_t dial_read(void)
{
  uint32_t sum = 0;
  for (unsigned i = 0; i < 4; i++) sum += adc_read();
  return (uint16_t)((sum + 2u) / 4u);
}

void game_audio_dial_init(void)
{
  adc_init();
#ifdef PIMORONI_PICO_PLUS2_RP2350
  /* The GP28 header is wired to both GP28 and ADC GP42 through 1k resistors.
   * Leave the digital partner high impedance; I2S + dial uses DIN on GP17. */
  gpio_init(28);
  gpio_set_dir(28, GPIO_IN);
  gpio_set_function(28, GPIO_FUNC_NULL);
  gpio_disable_pulls(28);
  gpio_set_input_enabled(28, false);
#endif
  adc_gpio_init(AUDIO_DIAL_GPIO);
  adc_select_input(AUDIO_DIAL_ADC);
  (void)adc_read();
  dial_raw = dial_read();
  dial_filtered_q8 = (uint32_t)dial_raw << 8;
  dial_volume = game_audio_dial_map(dial_raw);
  dial_polled_at = time_us_32();
  dial_ready = true;
  printf("AUDIO DIAL: GP%u ADC%u raw=%u master=%u poll_hz=50\n",
         AUDIO_DIAL_GPIO, AUDIO_DIAL_ADC, dial_raw, dial_volume);
}

void game_audio_dial_poll(void)
{
  uint32_t now = time_us_32();
  if (!dial_ready || (uint32_t)(now - dial_polled_at) < AUDIO_DIAL_INTERVAL_US) return;
  dial_polled_at = now;
  adc_select_input(AUDIO_DIAL_ADC);
  (void)adc_read();
  uint16_t raw = dial_read();
  dial_raw = raw;
  int32_t difference = (int32_t)((uint32_t)raw << 8) - (int32_t)dial_filtered_q8;
  dial_filtered_q8 = (uint32_t)((int32_t)dial_filtered_q8 + difference / 4);
  dial_volume = game_audio_dial_map((uint16_t)((dial_filtered_q8 + 128u) >> 8));
}

bool game_audio_dial_supported(void) { return dial_ready; }
uint16_t game_audio_dial_value(void) { return dial_raw; }
uint8_t game_audio_master_volume(void) { return dial_volume; }
#else
void game_audio_dial_init(void) { }
void game_audio_dial_poll(void) { }
bool game_audio_dial_supported(void) { return false; }
uint16_t game_audio_dial_value(void) { return 0; }
uint8_t game_audio_master_volume(void) { return 255; }
#endif

#if GAME_BATTERY_WARNING
/* Called on the input timer, serialized with the audio dial ADC access. */
static struct game_battery_state battery_state;
static uint32_t battery_last_poll;
void game_battery_init(void)
{
  adc_init();
  adc_gpio_init(40);
  gpio_init(24);
  gpio_put(24, 1);
  gpio_set_dir(24, GPIO_OUT);
  battery_last_poll = time_us_32();
  printf("BATTERY: GP40 ADC0, LED GP24, divider 1:2, low=2.2V recover=2.4V\n");
}
void game_battery_poll(void)
{
  uint32_t now = time_us_32();
  if ((uint32_t)(now - battery_last_poll) < 20000u) return;
  battery_last_poll = now;
  adc_select_input(0);
  (void)adc_read();
  uint32_t sum = 0;
  for (unsigned i = 0; i < 4; i++) sum += adc_read();
  uint16_t raw = (uint16_t)((sum + 2u) / 4u);
  game_battery_sample(&battery_state, raw);
  gpio_put(24, game_battery_led_on(battery_state.low, now));
}
#else
void game_battery_init(void) { }
void game_battery_poll(void) { }
#endif
