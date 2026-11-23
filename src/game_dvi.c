#include "game_dvi.h"
#include "game_platform.h"

#include <stdio.h>
#include <string.h>
#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/time.h>

#include "dvi.h"
#include "tmds_encode.h"

enum {
  SCANLINE_WORDS = GAME_DISPLAY_WIDTH / 2,
  TMDS_WORDS_PER_LANE = GAME_DISPLAY_WIDTH * 2 / DVI_SYMBOLS_PER_WORD,
  TMDS_BUFFER_COUNT = 5,
  FRAME_TIMEOUT_US = 100000
};

_Static_assert(GAME_DISPLAY_WIDTH == 320 && GAME_DISPLAY_HEIGHT == 240,
               "DVI timing requires a 320x240 source");
_Static_assert(DVI_VERTICAL_REPEAT == 2 && DVI_SYMBOLS_PER_WORD == 2,
               "DVI uses 2x scanout with two TMDS symbols per word");
_Static_assert(DVI_N_TMDS_BUFFERS == 0, "DVI uses only static TMDS buffers");

static struct dvi_inst dvi;
static uint32_t scanout[2][GAME_DISPLAY_HEIGHT][SCANLINE_WORDS];
static uint32_t tmds_lines[TMDS_BUFFER_COUNT][N_TMDS_LANES * TMDS_WORDS_PER_LANE];

/* Core 0 fills the back buffer and sets pending. Core 1 clears it only
 * after switching at source row zero, once it no longer reads the old front.
 * DMA accesses the separate TMDS lines, so it may still finish the old frame. */
static uint32_t pending;
static uint32_t started;
static uint32_t back = 1;

static void __not_in_flash_func(encode_scanline)(const uint32_t *pixels)
{
  uint32_t *line;
  queue_remove_blocking_u32(&dvi.q_tmds_free, &line);
  tmds_encode_data_channel_16bpp(pixels, line, GAME_DISPLAY_WIDTH,
                                 DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
  tmds_encode_data_channel_16bpp(pixels, line + TMDS_WORDS_PER_LANE,
                                 GAME_DISPLAY_WIDTH,
                                 DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
  tmds_encode_data_channel_16bpp(pixels, line + 2 * TMDS_WORDS_PER_LANE,
                                 GAME_DISPLAY_WIDTH,
                                 DVI_16BPP_RED_MSB, DVI_16BPP_RED_LSB);
  queue_add_blocking_u32(&dvi.q_tmds_valid, &line);
}

static void __not_in_flash_func(dvi_core1)(void)
{
  uint32_t front = 0;
  dvi_register_irqs_this_core(&dvi, DMA_IRQ_1);
  /* Pre-encode a blank first row. The DMA starts in vertical blanking and
   * the worker queues the remaining rows while the display synchronises. */
  encode_scanline(scanout[front][0]);
  dvi_start(&dvi);
  __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
  for (uint row = 1;; row = (row + 1) % GAME_DISPLAY_HEIGHT) {
    if (row == 0 && __atomic_load_n(&pending, __ATOMIC_ACQUIRE)) {
      front ^= 1;
      __atomic_store_n(&pending, 0, __ATOMIC_RELEASE);
    }
    encode_scanline(scanout[front][row]);
  }
}

bool game_dvi_init(void)
{
  dvi.timing = &dvi_timing_640x480p_60hz;
  if (clock_get_hz(clk_sys) != dvi.timing->bit_clk_khz * 1000u) {
    printf("DVI: unexpected system clock\n");
    return false;
  }
  dvi.ser_cfg = (struct dvi_serialiser_cfg) {
    .pio = pio1,
    .sm_tmds = {0, 1, 2},
    .pins_tmds = {36, 34, 32}, /* B, G, R positive pins */
    .pins_clk = 38,
    .invert_diffpairs = false
  };
  if (pio_set_gpio_base(dvi.ser_cfg.pio, 16) != PICO_OK) {
    printf("DVI: cannot configure PIO GPIO base 16\n");
    return false;
  }
  dvi_init(&dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
  for (uint i = 0; i < TMDS_BUFFER_COUNT; i++) {
    uint32_t *line = tmds_lines[i];
    queue_add_blocking_u32(&dvi.q_tmds_free, &line);
  }
  multicore_launch_core1(dvi_core1);
  uint32_t began = time_us_32();
  while (!__atomic_load_n(&started, __ATOMIC_ACQUIRE)) {
    if (time_us_32() - began >= FRAME_TIMEOUT_US) {
      printf("DVI: scanout startup timed out\n");
      return false;
    }
    tight_loop_contents();
  }
  printf("DVI: 640x480p60, RGB565 2x, PIO1 GP32..39, DMA IRQ1 on Core 1\n");
  return true;
}

bool game_dvi_present(const uint8_t *rgb565_be)
{
  uint32_t began = time_us_32();
  while (__atomic_load_n(&pending, __ATOMIC_ACQUIRE)) {
    if (time_us_32() - began >= FRAME_TIMEOUT_US) {
      printf("DVI: frame handoff timed out\n");
      return false;
    }
    tight_loop_contents();
  }

  /* RGB565 in the engine is high byte first. PicoDVI consumes two
   * little-endian pixels per word in the same left-to-right order. */
  for (uint y = 0; y < GAME_DISPLAY_HEIGHT; y++) {
    for (uint pair = 0; pair < SCANLINE_WORDS; pair++) {
      uint32_t pixels;
      memcpy(&pixels, rgb565_be, sizeof(pixels));
      rgb565_be += sizeof(pixels);
      scanout[back][y][pair] = ((pixels & 0x00ff00ffu) << 8) |
                              ((pixels & 0xff00ff00u) >> 8);
    }
  }
  back ^= 1;
  __atomic_store_n(&pending, 1, __ATOMIC_RELEASE);
  return true;
}
