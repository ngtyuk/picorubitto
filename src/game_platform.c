#include "game_platform.h"
#include "game_audio_dial.h"
#include "game_boot_chime.h"
#include "game_system_palette.h"
#include "game_kana_font.h"
#include "game_kanji_font.h"
#include "game_ascii_font.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>
#include <pico/time.h>
#if GAME_TFT_CORE1
#include <pico/multicore.h>
#endif
#include <hardware/dma.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/psram.h>
#include <hardware/pwm.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#if GAME_AUDIO_I2S
#include <hardware/pio.h>
#include "game_audio_i2s.pio.h"
#include "game_audio_i2s.h"
#endif

#include "ff.h"
#include "diskio.h"
#include "picorb_heap.h"

#define SD_SPI spi1
#define SD_SCK_PIN 10
#define SD_MISO_PIN 8
#define SD_MOSI_PIN 11
#define SD_CS_PIN 13

#define TFT_SPI spi0
#define TFT_SCK_PIN 18
#define TFT_MOSI_PIN 19
#define TFT_RST_PIN 20
#define TFT_DC_PIN 21
#define TFT_CS_PIN 22

/* GMT020-02 is a 240x320 ST7789V panel.  The game API exposes a 320x240
 * landscape surface, so the controller is put in ST7789 rotation 1. */
#define TFT_ST7789_LANDSCAPE_MADCTL 0xa0u /* MY | MV | RGB */

#define INPUT_COUNT 10
#define INPUT_DEBOUNCE_US 5000u

#ifndef GAME_TFT_STRIPE_ROWS
#define GAME_TFT_STRIPE_ROWS 32u
#endif

#ifndef GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS
#define GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS 256u
#endif

#if GAME_TFT_STRIPE_ROWS < 1
#error "GAME_TFT_STRIPE_ROWS must be positive"
#endif

#define TFT_STRIPE_ROWS GAME_TFT_STRIPE_ROWS
#define TFT_STRIPE_BYTES (GAME_DISPLAY_WIDTH * (size_t)TFT_STRIPE_ROWS * 2u)

#define GAME_AUDIO_MAX_WAV_BYTES (1024u * 1024u)
#define GAME_AUDIO_MAX_SE_BYTES (128u * 1024u)
#define GAME_AUDIO_SE_VOICES 4u
#define GAME_AUDIO_MAX_VOICES 5u
#define GAME_AUDIO_BGM_VOICE 4u
#if GAME_AUDIO_I2S
#define GAME_AUDIO_BUFFER_SAMPLES 512u
#else
#define GAME_AUDIO_BUFFER_SAMPLES 2048u
#endif
#define GAME_AUDIO_MIDDLE_LEVEL 128u
#define GAME_AUDIO_FADE_MS 10u
#define GAME_AUDIO_RAMP_STEPS 32u
#define GAME_FONT_MAX_BYTES (512u * 1024u)

/* Keep the conservative SRAM-stripe path as the default. RP2350 QMI/PSRAM
 * direct DMA can be enabled after validating the exact board/display pair. */
#ifndef GAME_TFT_DMA_FROM_PSRAM
#define GAME_TFT_DMA_FROM_PSRAM 0
#endif

#ifndef GAME_TFT_PIPELINE
#define GAME_TFT_PIPELINE 0
#endif

#ifndef GAME_TFT_CORE1
#define GAME_TFT_CORE1 0
#endif

#ifndef GAME_ENABLE_PROFILING
#define GAME_ENABLE_PROFILING 0
#endif

#ifndef GAME_3D_LINE_CLIP
#define GAME_3D_LINE_CLIP 1
#endif
#ifndef GAME_3D_PROJECTION_CACHE
#define GAME_3D_PROJECTION_CACHE 1
#endif
#ifndef GAME_3D_DEPTH_SRAM
#define GAME_3D_DEPTH_SRAM 1
#endif
#ifndef GAME_3D_MODEL_SHARED
#define GAME_3D_MODEL_SHARED 1
#endif
#ifndef GAME_3D_TRIANGLE_MODE
#define GAME_3D_TRIANGLE_MODE 2
#endif

#ifndef GAME_FRAMEBUFFER_IN_SRAM
#define GAME_FRAMEBUFFER_IN_SRAM 0
#endif

#ifndef GAME_ENABLE_DIRTY_RECT
#define GAME_ENABLE_DIRTY_RECT 0
#endif

#ifndef GAME_ENABLE_FRAME_DIFF
#define GAME_ENABLE_FRAME_DIFF 0
#endif

#ifndef GAME_ENABLE_BACKGROUND_LAYER
#define GAME_ENABLE_BACKGROUND_LAYER 0
#endif

#if GAME_ENABLE_FRAME_DIFF && !GAME_ENABLE_DIRTY_RECT
#error "GAME_ENABLE_FRAME_DIFF requires GAME_ENABLE_DIRTY_RECT"
#endif

#if GAME_ENABLE_BACKGROUND_LAYER && !GAME_ENABLE_DIRTY_RECT
#error "GAME_ENABLE_BACKGROUND_LAYER requires GAME_ENABLE_DIRTY_RECT"
#endif

#if GAME_ENABLE_BACKGROUND_LAYER && GAME_ENABLE_FRAME_DIFF
#error "GAME_ENABLE_BACKGROUND_LAYER and GAME_ENABLE_FRAME_DIFF are mutually exclusive"
#endif

#if GAME_TFT_PIPELINE && !GAME_ENABLE_DIRTY_RECT
#error "GAME_TFT_PIPELINE requires GAME_ENABLE_DIRTY_RECT"
#endif

#if GAME_TFT_PIPELINE && !GAME_FRAMEBUFFER_IN_SRAM
#error "GAME_TFT_PIPELINE requires GAME_FRAMEBUFFER_IN_SRAM"
#endif

#if GAME_TFT_PIPELINE && GAME_TFT_DMA_FROM_PSRAM
#error "GAME_TFT_PIPELINE cannot use GAME_TFT_DMA_FROM_PSRAM"
#endif

#if GAME_TFT_CORE1 && !GAME_ENABLE_DIRTY_RECT
#error "GAME_TFT_CORE1 requires GAME_ENABLE_DIRTY_RECT"
#endif

#if GAME_TFT_CORE1 && !GAME_ENABLE_BACKGROUND_LAYER
#error "GAME_TFT_CORE1 requires GAME_ENABLE_BACKGROUND_LAYER"
#endif

#if GAME_TFT_CORE1 && !GAME_FRAMEBUFFER_IN_SRAM
#error "GAME_TFT_CORE1 requires GAME_FRAMEBUFFER_IN_SRAM"
#endif

#if GAME_TFT_CORE1 && GAME_TFT_DMA_FROM_PSRAM
#error "GAME_TFT_CORE1 cannot use GAME_TFT_DMA_FROM_PSRAM"
#endif

#if GAME_TFT_CORE1 && GAME_TFT_PIPELINE
#error "GAME_TFT_CORE1 and GAME_TFT_PIPELINE are separate presenter modes"
#endif

#ifndef GAME_SPRITE_CACHE_BYTES
#define GAME_SPRITE_CACHE_BYTES 65536u
#endif

#ifndef GAME_TFT_SPI_HZ
#define GAME_TFT_SPI_HZ 75000000u
#endif

#define GAME_MEDIA_BASE_ADDRESS \
  (GAME_PSRAM_BASE_ADDRESS + GAME_PSRAM_VM_HEAP_SIZE)
#if GAME_FRAMEBUFFER_IN_SRAM
#define GAME_ASSET_BASE_ADDRESS GAME_MEDIA_BASE_ADDRESS
#else
#define GAME_ASSET_BASE_ADDRESS \
  (GAME_MEDIA_BASE_ADDRESS + GAME_FRAMEBUFFER_BYTES)
#endif
#define GAME_MEDIA_END_ADDRESS \
  (GAME_PSRAM_BASE_ADDRESS + GAME_PSRAM_TOTAL_BYTES)

static uint8_t *const psram_base = (uint8_t *)(uintptr_t)GAME_PSRAM_BASE_ADDRESS;
#if GAME_SPRITE_CACHE_BYTES > 0
/* Reusable sprite allocations; overflow falls back to the PSRAM asset heap. */
static uint8_t sprite_cache[GAME_SPRITE_CACHE_BYTES]
  __attribute__((aligned(8)));
#endif
static size_t sprite_cache_next;
static ESTALLOC *asset_heap;
static ESTALLOC *sprite_heap;
static size_t asset_resident_bytes;
#if GAME_FRAMEBUFFER_IN_SRAM
#if GAME_TFT_CORE1
static uint8_t framebuffers[2][GAME_FRAMEBUFFER_BYTES]
  __attribute__((aligned(4)));
static uint8_t *framebuffer = framebuffers[0];
static uint8_t current_framebuffer_index;
#else
static uint8_t framebuffer_storage[GAME_FRAMEBUFFER_BYTES]
  __attribute__((aligned(4)));
static uint8_t *const framebuffer = framebuffer_storage;
#endif
#else
static uint8_t *const framebuffer = (uint8_t *)(uintptr_t)GAME_MEDIA_BASE_ADDRESS;
#endif
static uint8_t *media_next = (uint8_t *)(uintptr_t)GAME_ASSET_BASE_ADDRESS;
static const uint8_t *background_source;
static size_t background_source_len;
static bool background_layer_ready;
#if GAME_ENABLE_BACKGROUND_LAYER
static bool background_explicit;
#endif
#if GAME_ENABLE_FRAME_DIFF
static uint8_t *frame_diff_previous;
static bool frame_diff_ready;
static bool frame_diff_valid;
#endif

struct game_sprite {
  uint8_t *pixels;
  bool cached;
  bool opaque;
  size_t bytes;
  uint16_t frame_width;
  uint16_t frame_height;
  uint16_t frame_count;
  uint16_t sheet_width;
  uint16_t key_color;
};

struct game_texture {
  uint8_t *pixels;
  bool opaque;
  size_t bytes;
  uint16_t width;
  uint16_t height;
  uint16_t key_color;
};

struct game_3d_camera {
  float x;
  float y;
  float z;
  float yaw;
  float pitch;
  float fov_degrees;
  float near_clip;
  float far_clip;
  float focal_length;
  float sin_yaw;
  float cos_yaw;
  float sin_pitch;
  float cos_pitch;
  float depth_scale;
  float depth_bias;
  int32_t depth_scale_fixed;
  int32_t depth_bias_fixed;
  int64_t x_fixed;
  int64_t y_fixed;
  int64_t z_fixed;
  int64_t focal_length_fixed;
  int64_t sin_yaw_fixed;
  int64_t cos_yaw_fixed;
  int64_t sin_pitch_fixed;
  int64_t cos_pitch_fixed;
};

struct game_3d_vertex {
  float x;
  float y;
  float z;
};

struct game_3d_fixed_vertex {
  int64_t x;
  int64_t y;
  int64_t z;
};

struct game_3d_projected_vertex {
  float x;
  float y;
  float inverse_z;
  int64_t x_fixed;
  int64_t y_fixed;
  int32_t inverse_z_fixed;
};

struct game_3d_projection_entry {
  struct game_3d_projected_vertex value;
  uint32_t generation;
};
static struct game_3d_projection_entry *game_3d_projections;
static uint32_t game_3d_projection_generation;

#define GAME_3D_DEPTH_BUFFER_BYTES \
  (GAME_DISPLAY_WIDTH * GAME_DISPLAY_HEIGHT * sizeof(uint16_t))
#define GAME_3D_FIXED_SHIFT 16
#define GAME_3D_FIXED_ONE (INT64_C(1) << GAME_3D_FIXED_SHIFT)
#define GAME_3D_FIXED_HALF (GAME_3D_FIXED_ONE >> 1)
#define GAME_3D_FIXED_COORD_LIMIT 4096.0f

static uint16_t *game_3d_depth_buffer;
#if GAME_TFT_CORE1 && GAME_3D_DEPTH_SRAM
#define GAME_3D_DEPTH_SRAM_PIXELS (160u * 120u)
static uint16_t game_3d_depth_low_storage[GAME_3D_DEPTH_SRAM_PIXELS]
  __attribute__((aligned(4)));
static uint16_t *game_3d_depth_psram;
#endif
#if !GAME_TFT_CORE1
static uint16_t game_3d_depth_storage[GAME_DISPLAY_WIDTH * GAME_DISPLAY_HEIGHT]
  __attribute__((aligned(4)));
#endif
static bool game_3d_frame_active;
static uint16_t game_3d_viewport_x;
static uint16_t game_3d_viewport_y;
static uint16_t game_3d_viewport_width = GAME_DISPLAY_WIDTH;
static uint16_t game_3d_viewport_height = GAME_DISPLAY_HEIGHT;
static uint16_t game_3d_internal_width = GAME_DISPLAY_WIDTH;
static uint16_t game_3d_internal_height = GAME_DISPLAY_HEIGHT;
static bool game_3d_scale_2x;
static struct game_3d_fixed_vertex *game_3d_indexed_vertices;
static struct game_3d_vertex
  game_3d_wireframe_float_vertices[GAME_3D_INDEXED_MAX_VERTICES];
static struct game_3d_camera game_3d_camera = {
  0.0f, 0.0f, 0.0f,
  0.0f, 0.0f,
  70.0f, 0.1f, 100.0f,
  228.5f,
  0.0f, 1.0f,
  0.0f, 1.0f,
  0.1001001f, -0.001001001f,
  6560, -66,
  0, 0, 0, 14974976,
  0, 65536, 0, 65536
};

/* Core 0 owns depth; the presenter only reads the color framebuffer. The
 * fixed reservation is reused for every small resolution, without allocation.
 * Keep the full-size PSRAM fallback so supported? still covers every size. */
static void
game_3d_select_depth_buffer(void)
{
#if GAME_TFT_CORE1 && GAME_3D_DEPTH_SRAM
  size_t pixels = (size_t)game_3d_internal_width * game_3d_internal_height;
  uint16_t *next = game_3d_depth_psram == NULL ? NULL :
    pixels <= GAME_3D_DEPTH_SRAM_PIXELS ? game_3d_depth_low_storage :
    game_3d_depth_psram;
  static uint16_t logged_width, logged_height;
  if (next != game_3d_depth_buffer || logged_width != game_3d_internal_width ||
      logged_height != game_3d_internal_height) {
    printf("3D: depth target=%s width=%u height=%u bytes=%u\n",
           next == NULL ? "unavailable" :
           next == game_3d_depth_low_storage ? "SRAM" : "PSRAM",
           game_3d_internal_width, game_3d_internal_height,
           (unsigned)(pixels * sizeof(uint16_t)));
    logged_width = game_3d_internal_width;
    logged_height = game_3d_internal_height;
  }
  game_3d_depth_buffer = next;
#endif
}

static void
game_3d_update_scale_mode(void)
{
  game_3d_scale_2x =
    (uint32_t)game_3d_internal_width * 2u == game_3d_viewport_width &&
    (uint32_t)game_3d_internal_height * 2u == game_3d_viewport_height;
}

struct game_font {
  uint8_t *data;
  size_t bytes;
  uint16_t first_char;
  uint16_t glyph_count;
  uint16_t size_count;
};

struct game_audio_sound {
  uint8_t *wav_data;
  size_t wav_bytes;
  const uint8_t *sample_data;
  size_t sample_size;
  uint32_t sample_rate;
  struct game_audio_sound *next;
  bool released;
};
static game_audio_sound_t *loaded_sounds;

struct game_audio_voice {
  const game_audio_sound_t *sound;
  size_t position;
  uint8_t volume;
  bool playing;
};

#if GAME_AUDIO_I2S
static int audio_dma_peer = -1;
static volatile uint32_t audio_i2s_blocks, audio_i2s_stalls, audio_i2s_late_refills;
static volatile uint32_t audio_i2s_late_irqs, audio_i2s_refill_max_us;
static PIO audio_pio = pio0;
static int audio_sm = -1;
static uint audio_program_offset;
static struct game_audio_resampler audio_resampler;
static struct game_audio_pcm_probe audio_pcm_probe;
#else
static uint audio_pwm_slice;
static uint audio_pwm_channel;
static int audio_dma_timer = -1;
#endif
static int audio_dma_channel = -1;
static uint32_t audio_sample_rate;
static struct game_audio_voice audio_voices[GAME_AUDIO_MAX_VOICES];
static bool audio_bgm_loop;
static volatile bool audio_playing;
static bool audio_output_ready;
static volatile bool audio_buffer_valid[2];
static volatile uint audio_current_buffer;
#if GAME_AUDIO_I2S
static uint32_t audio_dma_buffers[2][GAME_AUDIO_BUFFER_SAMPLES]
#else
static uint8_t audio_dma_buffers[2][GAME_AUDIO_BUFFER_SAMPLES]
#endif
#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
  __attribute__((aligned(2048)));
_Static_assert(sizeof(audio_dma_buffers[0]) == 2048, "I2S read ring must match one buffer");
#else
  __attribute__((aligned(4)));
#endif

static bool platform_ready;
static const char *platform_boot_error = "BOOT";
static bool tft_ready;
static bool sd_ready;
static FATFS sd_fs;
static int tft_dma_channel = -1;
#if GAME_ENABLE_DIRTY_RECT || (!GAME_FRAMEBUFFER_IN_SRAM && !GAME_TFT_DMA_FROM_PSRAM)
#if GAME_TFT_PIPELINE
static uint8_t tft_stripe[2][TFT_STRIPE_BYTES] __attribute__((aligned(4)));
#else
static uint8_t tft_stripe[TFT_STRIPE_BYTES] __attribute__((aligned(4)));
#endif
#endif
static repeating_timer_t sd_timer;
static repeating_timer_t input_timer;
static bool input_timer_ready;
static volatile uint32_t input_stable;
static uint32_t input_candidate;
static uint32_t input_changed_at[INPUT_COUNT];
static uint32_t input_frame_state;
static uint32_t input_previous_state;
static uint32_t frame_time_previous_us;
static uint32_t frame_delta_ms;
static absolute_time_t frame_deadline;
static bool frame_deadline_set;

#if GAME_ENABLE_DIRTY_RECT
#define GAME_DIRTY_RECT_LIMIT 64u

struct game_dirty_rect {
  uint16_t x0;
  uint16_t y0;
  uint16_t x1;
  uint16_t y1;
};

static struct game_dirty_rect dirty_rects[GAME_DIRTY_RECT_LIMIT];
static size_t dirty_rect_count;

#if GAME_ENABLE_BACKGROUND_LAYER
static struct game_dirty_rect dynamic_dirty_rects[GAME_DIRTY_RECT_LIMIT];
static size_t dynamic_dirty_rect_count;
#if GAME_TFT_CORE1
static struct game_dirty_rect framebuffer_previous_dynamic_rects[2]
  [GAME_DIRTY_RECT_LIMIT];
static size_t framebuffer_previous_dynamic_rect_counts[2];
#else
static struct game_dirty_rect previous_dynamic_rects[GAME_DIRTY_RECT_LIMIT];
static size_t previous_dynamic_rect_count;
#endif
#endif

#if GAME_TFT_CORE1
enum tft_core1_slot_state {
  TFT_CORE1_SLOT_FREE = 0,
  TFT_CORE1_SLOT_READY = 1,
  TFT_CORE1_SLOT_SENDING = 2
};

struct tft_core1_frame_slot {
  const uint8_t *framebuffer;
  struct game_dirty_rect rects[GAME_DIRTY_RECT_LIMIT];
  size_t rect_count;
#if GAME_ENABLE_PROFILING
  uint32_t copy_us;
  uint32_t window_us;
  uint32_t dma_wait_us;
  uint32_t spi_idle_us;
#endif
};

/* These are shared only through the two slot-state words. The frame data and
 * dirty-rectangle snapshot are published before READY, and are not touched
 * again until Core 1 changes the slot back to FREE. */
static struct tft_core1_frame_slot tft_core1_slots[2];
static volatile uint32_t tft_core1_slot_states[2];
static bool tft_core1_started;
static void tft_core1_wait_for_slot(uint32_t slot_index);
#endif

static bool
display_dirty_should_merge(const struct game_dirty_rect *left,
                            const struct game_dirty_rect *right)
{
  uint32_t left_width = (uint32_t)left->x1 - left->x0 + 1u;
  uint32_t left_height = (uint32_t)left->y1 - left->y0 + 1u;
  uint32_t right_width = (uint32_t)right->x1 - right->x0 + 1u;
  uint32_t right_height = (uint32_t)right->y1 - right->y0 + 1u;
  uint32_t union_x0 = left->x0 < right->x0 ? left->x0 : right->x0;
  uint32_t union_y0 = left->y0 < right->y0 ? left->y0 : right->y0;
  uint32_t union_x1 = left->x1 > right->x1 ? left->x1 : right->x1;
  uint32_t union_y1 = left->y1 > right->y1 ? left->y1 : right->y1;
  uint32_t union_width = union_x1 - union_x0 + 1u;
  uint32_t union_height = union_y1 - union_y0 + 1u;
  uint32_t separate_pixels = left_width * left_height +
                              right_width * right_height;
  uint32_t union_pixels = union_width * union_height;

  /* A TFT window has a fixed command/setup cost. It is often faster to send
   * a small gap between nearby sprites than to open another window. Limit
   * the extra pixel area so that sprites spread across the screen remain
   * separate. Apply the same cost limit to overlapping rectangles: a thin
   * cross must not turn into a nearly full-screen upload. */
  return union_pixels <= separate_pixels +
                         (uint32_t)GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS;
}

static void
display_dirty_add_to(struct game_dirty_rect *rects, size_t *count,
                     int x, int y, int width, int height)
{
  int64_t right;
  int64_t bottom;
  struct game_dirty_rect merged;

  if (width <= 0 || height <= 0) {
    return;
  }
  right = (int64_t)x + (int64_t)width;
  bottom = (int64_t)y + (int64_t)height;
  if (right <= 0 || bottom <= 0 || x >= (int)GAME_DISPLAY_WIDTH ||
      y >= (int)GAME_DISPLAY_HEIGHT) {
    return;
  }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (right > (int64_t)GAME_DISPLAY_WIDTH) right = GAME_DISPLAY_WIDTH;
  if (bottom > (int64_t)GAME_DISPLAY_HEIGHT) bottom = GAME_DISPLAY_HEIGHT;

  merged.x0 = (uint16_t)x;
  merged.y0 = (uint16_t)y;
  merged.x1 = (uint16_t)(right - 1);
  merged.y1 = (uint16_t)(bottom - 1);

  /* Combine regions while the bounding rectangle meets the transfer budget.
   * Keeping separate regions is important: a list of
   * sprites spread over the screen should not become one full-screen upload. */
  for (;;) {
    size_t index;
    bool combined = false;
    for (index = 0; index < *count; index++) {
      struct game_dirty_rect *current = &rects[index];
      if (!display_dirty_should_merge(&merged, current)) {
        continue;
      }
      if (current->x0 < merged.x0) merged.x0 = current->x0;
      if (current->y0 < merged.y0) merged.y0 = current->y0;
      if (current->x1 > merged.x1) merged.x1 = current->x1;
      if (current->y1 > merged.y1) merged.y1 = current->y1;
      rects[index] = rects[*count - 1];
      (*count)--;
      combined = true;
      break;
    }
    if (!combined) {
      break;
    }
  }

  if (*count >= GAME_DIRTY_RECT_LIMIT) {
    /* Correctness is more important than the optimization when a game has
     * unusually fragmented drawing. Fall back to one full-screen transfer. */
    *count = 1;
    rects[0].x0 = 0;
    rects[0].y0 = 0;
    rects[0].x1 = GAME_DISPLAY_WIDTH - 1;
    rects[0].y1 = GAME_DISPLAY_HEIGHT - 1;
    return;
  }
  rects[(*count)++] = merged;
}

static void
display_dirty_reset(void)
{
  dirty_rect_count = 0;
#if GAME_ENABLE_BACKGROUND_LAYER
  dynamic_dirty_rect_count = 0;
#endif
}

static void
display_dirty_add(int x, int y, int width, int height)
{
  display_dirty_add_to(dirty_rects, &dirty_rect_count,
                       x, y, width, height);
#if GAME_ENABLE_BACKGROUND_LAYER
  display_dirty_add_to(dynamic_dirty_rects, &dynamic_dirty_rect_count,
                       x, y, width, height);
#endif
}

#if GAME_ENABLE_BACKGROUND_LAYER
static void
display_dirty_add_background(int x, int y, int width, int height)
{
  display_dirty_add_to(dirty_rects, &dirty_rect_count,
                       x, y, width, height);
}
#endif
#else
static inline void
display_dirty_reset(void)
{
}

static inline void
display_dirty_add(int x, int y, int width, int height)
{
  (void)x;
  (void)y;
  (void)width;
  (void)height;
}
#endif

#if GAME_ENABLE_PROFILING
static bool profile_frame_active;
static uint32_t profile_frame_start;
static uint32_t profile_clear_us;
static uint32_t profile_background_us;
static uint32_t profile_diff_us;
static uint32_t profile_rect_us;
static uint32_t profile_sprite_us;
static uint32_t profile_sprite_cached_us;
static uint32_t profile_sprite_psram_us;
static uint32_t profile_text_us;
static uint32_t profile_present_us;
static uint32_t profile_present_copy_us;
static uint32_t profile_present_window_us;
static uint32_t profile_present_dma_wait_us;
static uint32_t profile_present_spi_idle_us;
static uint32_t profile_frame_us;
static uint32_t profile_other_us;
static uint32_t profile_sprite_calls;
static uint32_t profile_sprite_cached_calls;
static uint32_t profile_present_dma_submits;
static uint32_t profile_present_pipeline_stalls;
static uint32_t profile_window_start;
static uint32_t profile_window_frames;
static uint32_t profile_window_clear_us;
static uint32_t profile_window_background_us;
static uint32_t profile_window_diff_us;
static uint32_t profile_window_rect_us;
static uint32_t profile_window_sprite_us;
static uint32_t profile_window_sprite_cached_us;
static uint32_t profile_window_sprite_psram_us;
static uint32_t profile_window_text_us;
static uint32_t profile_window_present_us;
static uint32_t profile_window_present_copy_us;
static uint32_t profile_window_present_window_us;
static uint32_t profile_window_present_dma_wait_us;
static uint32_t profile_window_present_spi_idle_us;
static uint32_t profile_window_frame_us;
static uint32_t profile_window_other_us;
static uint32_t profile_window_sprite_calls;
static uint32_t profile_window_sprite_cached_calls;
static uint32_t profile_window_present_dma_submits;
static uint32_t profile_window_present_pipeline_stalls;
#if GAME_TFT_CORE1
static uint32_t profile_present_core1_wait_us;
static uint32_t profile_present_core1_submits;
static uint32_t profile_present_core1_stalls;
static uint32_t profile_window_present_core1_wait_us;
static uint32_t profile_window_present_core1_submits;
static uint32_t profile_window_present_core1_stalls;
#endif
static uint32_t profile_dirty_rects;
static uint32_t profile_dirty_bytes;
static uint32_t profile_window_dirty_rects;
static uint32_t profile_window_dirty_bytes;
#endif

/* UP, DOWN, LEFT, RIGHT, A, B, L, R, START, SELECT. */
static const uint8_t button_pins[INPUT_COUNT] = {
  2, 3, 4, 5, 6, 7, 9, 12, 14, 16
};

/* This low-level entry point is supplied by PicoRuby's FatFs SPI port. */
extern int FAT_set_spi_unit(const char *name, int sck, int cipo, int copi, int cs);
extern void disk_timerproc(void);

static void audio_init(void);
static void audio_boot_chime(void);
static void fill_rgb565_row(uint8_t *destination, size_t pixels,
                            uint16_t color);
static void draw_sprite_transparent_row(const uint8_t *source,
                                        uint8_t *destination, size_t pixels,
                                        uint16_t key_color);

#if GAME_ENABLE_PROFILING
static uint32_t
profile_now(void)
{
  return time_us_32();
}

static void
profile_begin_frame(void)
{
  uint32_t now;

  if (profile_frame_active) {
    return;
  }
  now = profile_now();
  if (profile_window_frames == 0) {
    profile_window_start = now;
  }
  profile_frame_start = now;
  profile_clear_us = 0;
  profile_background_us = 0;
  profile_diff_us = 0;
  profile_rect_us = 0;
  profile_sprite_us = 0;
  profile_sprite_cached_us = 0;
  profile_sprite_psram_us = 0;
  profile_text_us = 0;
  profile_present_us = 0;
  profile_present_copy_us = 0;
  profile_present_window_us = 0;
  profile_present_dma_wait_us = 0;
  profile_present_spi_idle_us = 0;
  profile_frame_us = 0;
  profile_other_us = 0;
  profile_sprite_calls = 0;
  profile_sprite_cached_calls = 0;
  profile_present_dma_submits = 0;
  profile_present_pipeline_stalls = 0;
#if GAME_TFT_CORE1
  profile_present_core1_wait_us = 0;
  profile_present_core1_submits = 0;
  profile_present_core1_stalls = 0;
#endif
  profile_dirty_rects = 0;
  profile_dirty_bytes = 0;
  profile_frame_active = true;
}

static uint32_t
profile_start(void)
{
  profile_begin_frame();
  return profile_now();
}

static void
profile_add(uint32_t *total, uint32_t started)
{
  *total += profile_now() - started;
}

static uint32_t
profile_average(uint32_t total, uint32_t frames)
{
  return frames == 0 ? 0 : total / frames;
}

static void
profile_finish_frame(uint32_t present_started)
{
  uint32_t now = profile_now();
  uint32_t accounted_us;

  profile_present_us += now - present_started;
  profile_frame_us = now - profile_frame_start;
  accounted_us = profile_clear_us + profile_background_us +
                 profile_diff_us + profile_rect_us + profile_sprite_us +
                 profile_text_us + profile_present_us;
  profile_other_us = profile_frame_us > accounted_us
                     ? profile_frame_us - accounted_us : 0;
  profile_window_frames++;
  profile_window_clear_us += profile_clear_us;
  profile_window_background_us += profile_background_us;
  profile_window_diff_us += profile_diff_us;
  profile_window_rect_us += profile_rect_us;
  profile_window_sprite_us += profile_sprite_us;
  profile_window_sprite_cached_us += profile_sprite_cached_us;
  profile_window_sprite_psram_us += profile_sprite_psram_us;
  profile_window_text_us += profile_text_us;
  profile_window_present_us += profile_present_us;
  profile_window_present_copy_us += profile_present_copy_us;
  profile_window_present_window_us += profile_present_window_us;
  profile_window_present_dma_wait_us += profile_present_dma_wait_us;
  profile_window_present_spi_idle_us += profile_present_spi_idle_us;
  profile_window_frame_us += profile_frame_us;
  profile_window_other_us += profile_other_us;
  profile_window_sprite_calls += profile_sprite_calls;
  profile_window_sprite_cached_calls += profile_sprite_cached_calls;
  profile_window_present_dma_submits += profile_present_dma_submits;
  profile_window_present_pipeline_stalls += profile_present_pipeline_stalls;
#if GAME_TFT_CORE1
  profile_window_present_core1_wait_us += profile_present_core1_wait_us;
  profile_window_present_core1_submits += profile_present_core1_submits;
  profile_window_present_core1_stalls += profile_present_core1_stalls;
#endif
  profile_window_dirty_rects += profile_dirty_rects;
  profile_window_dirty_bytes += profile_dirty_bytes;
  profile_frame_active = false;
}

static void
profile_report(void)
{
  uint32_t now;
  uint32_t elapsed;
  uint32_t frames;

  if (profile_window_frames == 0) {
    return;
  }
  now = profile_now();
  elapsed = now - profile_window_start;
  if (elapsed < 1000000u) {
    return;
  }

  frames = profile_window_frames;
  printf("PERF fps=%lu frames=%lu sprites=%lu sprite_cached=%lu "
         "clear_us=%lu "
         "background_us=%lu diff_us=%lu rect_us=%lu sprite_us=%lu "
         "sprite_cached_us=%lu sprite_psram_us=%lu text_us=%lu "
         "present_us=%lu frame_us=%lu other_us=%lu "
         "dirty_rects=%lu dirty_bytes=%lu "
         "present_copy_us=%lu present_window_us=%lu "
         "present_dma_wait_us=%lu present_spi_idle_us=%lu "
         "present_dma_submits=%lu present_pipeline_stalls=%lu"
#if GAME_TFT_CORE1
         " present_core1_wait_us=%lu present_core1_submits=%lu"
         " present_core1_stalls=%lu"
#endif
         "\n",
         (unsigned long)((uint32_t)frames * 1000000u / elapsed),
         (unsigned long)frames,
         (unsigned long)profile_average(profile_window_sprite_calls, frames),
         (unsigned long)profile_average(profile_window_sprite_cached_calls,
                                        frames),
         (unsigned long)profile_average(profile_window_clear_us, frames),
         (unsigned long)profile_average(profile_window_background_us, frames),
         (unsigned long)profile_average(profile_window_diff_us, frames),
         (unsigned long)profile_average(profile_window_rect_us, frames),
         (unsigned long)profile_average(profile_window_sprite_us, frames),
         (unsigned long)profile_average(profile_window_sprite_cached_us,
                                        frames),
         (unsigned long)profile_average(profile_window_sprite_psram_us,
                                        frames),
         (unsigned long)profile_average(profile_window_text_us, frames),
         (unsigned long)profile_average(profile_window_present_us, frames),
         (unsigned long)profile_average(profile_window_frame_us, frames),
         (unsigned long)profile_average(profile_window_other_us, frames),
         (unsigned long)profile_average(profile_window_dirty_rects, frames),
         (unsigned long)profile_average(profile_window_dirty_bytes, frames),
         (unsigned long)profile_average(profile_window_present_copy_us, frames),
         (unsigned long)profile_average(profile_window_present_window_us, frames),
         (unsigned long)profile_average(profile_window_present_dma_wait_us, frames),
         (unsigned long)profile_average(profile_window_present_spi_idle_us, frames),
         (unsigned long)profile_average(profile_window_present_dma_submits, frames),
         (unsigned long)profile_average(profile_window_present_pipeline_stalls,
                                        frames)
#if GAME_TFT_CORE1
         , (unsigned long)profile_average(profile_window_present_core1_wait_us,
                                           frames)
         , (unsigned long)profile_average(profile_window_present_core1_submits,
                                           frames)
         , (unsigned long)profile_average(profile_window_present_core1_stalls,
                                           frames)
#endif
         );

  profile_window_start = now;
  profile_window_frames = 0;
  profile_window_clear_us = 0;
  profile_window_background_us = 0;
  profile_window_diff_us = 0;
  profile_window_rect_us = 0;
  profile_window_sprite_us = 0;
  profile_window_sprite_cached_us = 0;
  profile_window_sprite_psram_us = 0;
  profile_window_text_us = 0;
  profile_window_present_us = 0;
  profile_window_present_copy_us = 0;
  profile_window_present_window_us = 0;
  profile_window_present_dma_wait_us = 0;
  profile_window_present_spi_idle_us = 0;
  profile_window_frame_us = 0;
  profile_window_other_us = 0;
  profile_window_sprite_calls = 0;
  profile_window_sprite_cached_calls = 0;
  profile_window_present_dma_submits = 0;
  profile_window_present_pipeline_stalls = 0;
#if GAME_TFT_CORE1
  profile_window_present_core1_wait_us = 0;
  profile_window_present_core1_submits = 0;
  profile_window_present_core1_stalls = 0;
#endif
  profile_window_dirty_rects = 0;
  profile_window_dirty_bytes = 0;
}

static void
profile_reset(void)
{
  profile_frame_active = false;
  profile_window_start = profile_now();
  profile_window_frames = 0;
  profile_window_clear_us = 0;
  profile_window_background_us = 0;
  profile_window_diff_us = 0;
  profile_window_rect_us = 0;
  profile_window_sprite_us = 0;
  profile_window_sprite_cached_us = 0;
  profile_window_sprite_psram_us = 0;
  profile_window_text_us = 0;
  profile_window_present_us = 0;
  profile_window_present_copy_us = 0;
  profile_window_present_window_us = 0;
  profile_window_present_dma_wait_us = 0;
  profile_window_present_spi_idle_us = 0;
  profile_window_frame_us = 0;
  profile_window_other_us = 0;
  profile_window_sprite_calls = 0;
  profile_window_sprite_cached_calls = 0;
  profile_window_present_dma_submits = 0;
  profile_window_present_pipeline_stalls = 0;
#if GAME_TFT_CORE1
  profile_window_present_core1_wait_us = 0;
  profile_window_present_core1_submits = 0;
  profile_window_present_core1_stalls = 0;
#endif
  profile_window_dirty_rects = 0;
  profile_window_dirty_bytes = 0;
}
#endif

static void *
media_alloc(size_t bytes, size_t alignment)
{
  uintptr_t address = (uintptr_t)media_next;
  uintptr_t end;

  if (asset_heap != NULL || alignment == 0 || (alignment & (alignment - 1u)) != 0) {
    return NULL;
  }
  address = (address + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
  if (address > GAME_MEDIA_END_ADDRESS ||
      bytes > (size_t)(GAME_MEDIA_END_ADDRESS - address)) {
    return NULL;
  }
  end = address + bytes;
  if (end > GAME_MEDIA_END_ADDRESS) {
    return NULL;
  }
  media_next = (uint8_t *)end;
  return (void *)address;
}

/* Permanent boot/ROM storage stays below this independent, coalescing heap.
 * Only Core 0 allocates/frees; the audio IRQ only reads live sound data. */
static void *
asset_alloc(size_t bytes)
{
  if (bytes == 0 || bytes > UINT_MAX) return NULL;
  if (asset_heap == NULL) {
    uintptr_t start = ((uintptr_t)media_next + 7u) & ~(uintptr_t)7u;
    if (start >= GAME_MEDIA_END_ADDRESS || GAME_MEDIA_END_ADDRESS - start < 1024u) return NULL;
    asset_heap = est_init((void *)start, (unsigned)(GAME_MEDIA_END_ADDRESS - start));
    if (asset_heap == NULL) return NULL;
  }
  return est_calloc(asset_heap, 1u, (unsigned)bytes);
}

static void
asset_free(void *ptr)
{
  if (ptr != NULL) est_free(asset_heap, ptr);
}

static void
sprite_cache_free(void *ptr)
{
  if (ptr != NULL && sprite_heap != NULL) {
    est_free(sprite_heap, ptr);
    est_take_statistics(sprite_heap);
    sprite_cache_next = sprite_heap->stat.used;
  }
}

static bool
background_source_is_new(const uint8_t *data, size_t data_len)
{
  return !background_layer_ready || background_source != data ||
         background_source_len != data_len;
}

#if GAME_ENABLE_BACKGROUND_LAYER
static void
background_restore_rect(const struct game_dirty_rect *rect)
{
  size_t width_bytes = ((size_t)rect->x1 - (size_t)rect->x0 + 1u) * 2u;

  for (uint32_t y = rect->y0; y <= rect->y1; y++) {
    size_t offset = ((size_t)y * (size_t)GAME_DISPLAY_WIDTH + rect->x0) * 2u;
    memcpy(framebuffer + offset, background_source + offset, width_bytes);
  }
}

static void
background_register(const uint8_t *data, size_t data_len)
{
  /* A new source replaces the backing store and invalidates all old dynamic
   * regions. The complete frame is uploaded once by the current present. */
#if GAME_TFT_CORE1
  if (tft_core1_started) {
    /* A background change rewrites both framebuffers. Wait until Core 1 has
     * released both before replacing either one. */
    tft_core1_wait_for_slot(0);
    tft_core1_wait_for_slot(1);
  }
  memcpy(framebuffers[0], data, GAME_FRAMEBUFFER_BYTES);
  memcpy(framebuffers[1], data, GAME_FRAMEBUFFER_BYTES);
  framebuffer_previous_dynamic_rect_counts[0] = 0;
  framebuffer_previous_dynamic_rect_counts[1] = 0;
#else
  memcpy(framebuffer, data, GAME_FRAMEBUFFER_BYTES);
#endif
  background_source = data;
  background_source_len = data_len;
#if !GAME_TFT_CORE1
  previous_dynamic_rect_count = 0;
#endif
  dynamic_dirty_rect_count = 0;
  background_layer_ready = true;
  display_dirty_add_background(0, 0, GAME_DISPLAY_WIDTH,
                               GAME_DISPLAY_HEIGHT);
  printf("BACKGROUND: static source registered bytes=%lu\n",
         (unsigned long)data_len);
}

static void
background_restore_previous(void)
{
  /* Restore the previous frame's moving objects before the game draws their
   * new positions. Restore regions are transfer-dirty, but are not remembered
   * as this frame's dynamic regions. */
#if GAME_TFT_CORE1
  const struct game_dirty_rect *previous =
    framebuffer_previous_dynamic_rects[current_framebuffer_index];
  size_t previous_count =
    framebuffer_previous_dynamic_rect_counts[current_framebuffer_index];
#else
  const struct game_dirty_rect *previous = previous_dynamic_rects;
  size_t previous_count = previous_dynamic_rect_count;
#endif

  for (size_t index = 0; index < previous_count; index++) {
    const struct game_dirty_rect *rect = &previous[index];
    background_restore_rect(rect);
    display_dirty_add_background(rect->x0, rect->y0,
                                 (int)rect->x1 - (int)rect->x0 + 1,
                                 (int)rect->y1 - (int)rect->y0 + 1);
  }
}

static void
background_finish_frame(void)
{
  if (!background_layer_ready) {
    return;
  }
#if GAME_TFT_CORE1
  size_t count = dynamic_dirty_rect_count;
  framebuffer_previous_dynamic_rect_counts[current_framebuffer_index] = count;
  if (count > 0) {
    memcpy(framebuffer_previous_dynamic_rects[current_framebuffer_index],
           dynamic_dirty_rects, count * sizeof(dynamic_dirty_rects[0]));
  }
#else
  previous_dynamic_rect_count = dynamic_dirty_rect_count;
  if (previous_dynamic_rect_count > 0) {
    memcpy(previous_dynamic_rects, dynamic_dirty_rects,
           previous_dynamic_rect_count * sizeof(dynamic_dirty_rects[0]));
  }
#endif
}
#endif

bool
game_display_set_background(const uint8_t *data, size_t data_len)
{
  if (data == NULL || data_len != GAME_FRAMEBUFFER_BYTES) {
    return false;
  }

#if GAME_ENABLE_BACKGROUND_LAYER
  background_explicit = true;
  if (background_source_is_new(data, data_len)) {
    background_register(data, data_len);
  }
#else
  if (background_source_is_new(data, data_len)) {
    memcpy(framebuffer, data, GAME_FRAMEBUFFER_BYTES);
    background_source = data;
    background_source_len = data_len;
    background_layer_ready = true;
    display_dirty_add(0, 0, GAME_DISPLAY_WIDTH, GAME_DISPLAY_HEIGHT);
  }
#endif
  return true;
}

const uint8_t *
game_display_background_source(void)
{
  return background_source;
}

void
game_display_clear_background(void)
{
  background_source = NULL;
  background_source_len = 0;
  background_layer_ready = false;
#if GAME_ENABLE_BACKGROUND_LAYER
  background_explicit = false;
#endif
}

void
game_display_begin_frame(void)
{
#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif
#if GAME_ENABLE_BACKGROUND_LAYER
  if (background_layer_ready) {
    background_restore_previous();
  }
#else
  if (background_layer_ready) {
    memcpy(framebuffer, background_source, GAME_FRAMEBUFFER_BYTES);
    display_dirty_add(0, 0, GAME_DISPLAY_WIDTH, GAME_DISPLAY_HEIGHT);
  }
#endif
#if GAME_ENABLE_PROFILING
  profile_add(&profile_background_us, profile_started);
#endif
}

#if GAME_ENABLE_BACKGROUND_LAYER
static void
background_legacy_full_blit(const uint8_t *data, size_t data_len)
{
  if (background_source_is_new(data, data_len)) {
    background_register(data, data_len);
  } else {
    background_restore_previous();
  }
}
#endif

#if GAME_ENABLE_FRAME_DIFF
static void
display_diff_flush_pending(struct game_dirty_rect *pending,
                           bool *pending_valid)
{
  if (!*pending_valid) {
    return;
  }
  display_dirty_add(pending->x0, pending->y0,
                    (int)pending->x1 - (int)pending->x0 + 1,
                    (int)pending->y1 - (int)pending->y0 + 1);
  *pending_valid = false;
}

static void
display_diff_build(void)
{
  const size_t row_bytes = (size_t)GAME_DISPLAY_WIDTH * 2u;
  struct game_dirty_rect pending = {0, 0, 0, 0};
  bool pending_valid = false;

  display_dirty_reset();
  if (!frame_diff_valid) {
    display_dirty_add(0, 0, GAME_DISPLAY_WIDTH, GAME_DISPLAY_HEIGHT);
    return;
  }

  /* Compare completed frames rather than trusting the draw-call rectangles.
   * This lets a game redraw a static full-screen background without forcing a
   * full TFT upload. Each changed row is reduced to one conservative span;
   * adjacent spans are combined to keep the TFT window count bounded. */
  for (uint32_t y = 0; y < GAME_DISPLAY_HEIGHT; y++) {
    const uint8_t *current = framebuffer + (size_t)y * row_bytes;
    const uint8_t *previous = frame_diff_previous + (size_t)y * row_bytes;
    uint32_t first = GAME_DISPLAY_WIDTH;
    uint32_t last = 0;

    if (memcmp(current, previous, row_bytes) == 0) {
      display_diff_flush_pending(&pending, &pending_valid);
      continue;
    }
    for (uint32_t x = 0; x < GAME_DISPLAY_WIDTH; x++) {
      size_t offset = (size_t)x * 2u;
      if (current[offset] != previous[offset] ||
          current[offset + 1u] != previous[offset + 1u]) {
        if (first == GAME_DISPLAY_WIDTH) {
          first = x;
        }
        last = x;
      }
    }
    if (first == GAME_DISPLAY_WIDTH) {
      display_diff_flush_pending(&pending, &pending_valid);
      continue;
    }

    if (pending_valid && y == (uint32_t)pending.y1 + 1u &&
        first <= (uint32_t)pending.x1 + 1u &&
        last + 1u >= (uint32_t)pending.x0) {
      if (first < pending.x0) pending.x0 = (uint16_t)first;
      if (last > pending.x1) pending.x1 = (uint16_t)last;
      pending.y1 = (uint16_t)y;
    } else {
      display_diff_flush_pending(&pending, &pending_valid);
      pending.x0 = (uint16_t)first;
      pending.y0 = (uint16_t)y;
      pending.x1 = (uint16_t)last;
      pending.y1 = (uint16_t)y;
      pending_valid = true;
    }
  }
  display_diff_flush_pending(&pending, &pending_valid);
}

static void
display_diff_commit(void)
{
  if (!frame_diff_valid) {
    memcpy(frame_diff_previous, framebuffer, GAME_FRAMEBUFFER_BYTES);
    frame_diff_valid = true;
    return;
  }

  /* Only update the shadow for changed regions. If no pixels changed, the
   * previous frame is already current and this avoids a full PSRAM write. */
  for (size_t index = 0; index < dirty_rect_count; index++) {
    const struct game_dirty_rect *rect = &dirty_rects[index];
    size_t width_bytes = ((size_t)rect->x1 - (size_t)rect->x0 + 1u) * 2u;
    for (uint32_t y = rect->y0; y <= rect->y1; y++) {
      size_t offset = ((size_t)y * (size_t)GAME_DISPLAY_WIDTH + rect->x0) * 2u;
      memcpy(frame_diff_previous + offset, framebuffer + offset, width_bytes);
    }
  }
}
#endif

static uint8_t *
sprite_cache_alloc(size_t bytes, size_t alignment)
{
#if GAME_SPRITE_CACHE_BYTES == 0
  (void)bytes; (void)alignment;
  return NULL;
#else
  if (bytes == 0 || bytes > UINT_MAX || alignment == 0 || alignment > 8u ||
      (alignment & (alignment - 1u)) != 0) return NULL;
  if (sprite_heap == NULL) sprite_heap = est_init(sprite_cache, sizeof(sprite_cache));
  if (sprite_heap == NULL) return NULL;
  void *result = est_malloc(sprite_heap, (unsigned)bytes);
  est_take_statistics(sprite_heap);
  sprite_cache_next = sprite_heap->stat.used;
  return result;
#endif
}

static bool
sd_timer_callback(repeating_timer_t *timer)
{
  (void)timer;
  disk_timerproc();
  return true;
}

static void
tft_select(void)
{
  gpio_put(TFT_CS_PIN, 0);
}

static void
tft_deselect(void)
{
  gpio_put(TFT_CS_PIN, 1);
}

static void
tft_command(uint8_t command)
{
  tft_select();
  gpio_put(TFT_DC_PIN, 0);
  spi_write_blocking(TFT_SPI, &command, 1);
  tft_deselect();
}

static void
tft_command_data(uint8_t command, const uint8_t *data, size_t length)
{
  tft_select();
  gpio_put(TFT_DC_PIN, 0);
  spi_write_blocking(TFT_SPI, &command, 1);
  if (length > 0) {
    gpio_put(TFT_DC_PIN, 1);
    spi_write_blocking(TFT_SPI, data, length);
  }
  tft_deselect();
}

static void
tft_wait_for_idle(void)
{
  /* DMA completion means that the last byte reached the SPI FIFO, not
   * necessarily that the SPI shifter has finished putting it on the wire.
   * Keep CS asserted until the controller is truly idle. */
  while (spi_is_busy(TFT_SPI)) {
    tight_loop_contents();
  }
}

static void
tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t data[4];

  /* CASET and PASET are part of one command sequence. Keeping CS asserted
   * across both commands avoids two extra chip-select transitions for every
   * dirty rectangle without changing the controller protocol. */
  tft_select();
  gpio_put(TFT_DC_PIN, 0);
  data[0] = 0x2a;
  spi_write_blocking(TFT_SPI, data, 1);
  gpio_put(TFT_DC_PIN, 1);
  data[0] = (uint8_t)(x0 >> 8);
  data[1] = (uint8_t)x0;
  data[2] = (uint8_t)(x1 >> 8);
  data[3] = (uint8_t)x1;
  spi_write_blocking(TFT_SPI, data, sizeof(data));

  gpio_put(TFT_DC_PIN, 0);
  data[0] = 0x2b;
  spi_write_blocking(TFT_SPI, data, 1);
  gpio_put(TFT_DC_PIN, 1);
  data[0] = (uint8_t)(y0 >> 8);
  data[1] = (uint8_t)y0;
  data[2] = (uint8_t)(y1 >> 8);
  data[3] = (uint8_t)y1;
  spi_write_blocking(TFT_SPI, data, sizeof(data));
  tft_deselect();
}

static void
tft_init(void)
{
  uint actual_spi_hz = spi_init(TFT_SPI, GAME_TFT_SPI_HZ);
  printf("TFT: init ST7789V SPI0 requested=%lu MHz actual=%lu kHz, "
         "320x240 landscape\n",
         (unsigned long)(GAME_TFT_SPI_HZ / 1000000u),
         (unsigned long)(actual_spi_hz / 1000u));
  gpio_set_function(TFT_SCK_PIN, GPIO_FUNC_SPI);
  gpio_set_function(TFT_MOSI_PIN, GPIO_FUNC_SPI);

  gpio_init(TFT_RST_PIN);
  gpio_set_dir(TFT_RST_PIN, GPIO_OUT);
  gpio_init(TFT_DC_PIN);
  gpio_set_dir(TFT_DC_PIN, GPIO_OUT);
  gpio_init(TFT_CS_PIN);
  gpio_set_dir(TFT_CS_PIN, GPIO_OUT);
  gpio_put(TFT_CS_PIN, 1);
  gpio_put(TFT_DC_PIN, 1);

  gpio_put(TFT_RST_PIN, 0);
  sleep_ms(10);
  gpio_put(TFT_RST_PIN, 1);
  sleep_ms(120);

  /* ST7789V initialization.  The previous sequence was for an ILI9341;
   * those controller-specific power and display-function registers leave an
   * ST7789V with an invalid address mapping, which appears as noise and
   * repeated bands on the panel. */
  tft_command(0x01); /* SWRESET */
  sleep_ms(150);
  tft_command(0x11); /* SLPOUT */
  sleep_ms(10);

  static const uint8_t pixel_format[] = {0x55}; /* 16-bit RGB565 */
  static const uint8_t memory_access[] = {
    TFT_ST7789_LANDSCAPE_MADCTL
  };
  tft_command_data(0x3a, pixel_format, sizeof(pixel_format));
  tft_command_data(0x36, memory_access, sizeof(memory_access));
  tft_command(0x21); /* INVON: required by this ST7789 panel family */
  sleep_ms(10);
  tft_command(0x13); /* NORON */
  sleep_ms(10);
  tft_command(0x29); /* DISPON */
  sleep_ms(10);

  tft_dma_channel = dma_claim_unused_channel(false);
  tft_ready = true;
  printf("TFT: ready, DMA channel=%d\n", tft_dma_channel);
}

static uint32_t input_poll_state(void);

static bool
input_timer_callback(repeating_timer_t *timer)
{
  (void)timer;
  input_poll_state();
  game_audio_dial_poll();
  game_battery_poll();
  return true;
}

static void
input_init(void)
{
  for (size_t i = 0; i < INPUT_COUNT; i++) {
    gpio_init(button_pins[i]);
    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
  }
  input_stable = 0;
  input_candidate = 0;
  input_frame_state = 0;
  input_previous_state = 0;
  uint32_t now = time_us_32();
  for (size_t i = 0; i < INPUT_COUNT; i++) input_changed_at[i] = now;
  game_audio_dial_init();
  game_battery_init();
  input_timer_ready = add_repeating_timer_ms(-1, input_timer_callback, NULL,
                                              &input_timer);
  if (!input_timer_ready) {
    printf("INPUT: timer unavailable; using frame polling\n");
  }
}

static bool
sd_init(void)
{
  DSTATUS status;
  FRESULT result;

  printf("SD: init SPI1 at 400 kHz\n");
  spi_init(SD_SPI, 400 * 1000);
  gpio_set_function(SD_SCK_PIN, GPIO_FUNC_SPI);
  gpio_set_function(SD_MOSI_PIN, GPIO_FUNC_SPI);
  gpio_set_function(SD_MISO_PIN, GPIO_FUNC_SPI);

  gpio_init(SD_CS_PIN);
  gpio_set_dir(SD_CS_PIN, GPIO_OUT);
  gpio_put(SD_CS_PIN, 1);

  if (FAT_set_spi_unit("RP2040_SPI1", SD_SCK_PIN, SD_MISO_PIN,
                       SD_MOSI_PIN, SD_CS_PIN) < 0) {
    printf("SD: FAT_set_spi_unit failed\n");
    return false;
  }

  add_repeating_timer_ms(-1, sd_timer_callback, NULL, &sd_timer);
  status = disk_initialize(2);
  printf("SD: disk_initialize status=0x%02x\n", status);
  if (status & STA_NOINIT) {
    return false;
  }

  /* The card has completed its low-speed negotiation. */
  spi_set_baudrate(SD_SPI, 25 * 1000 * 1000);
  result = f_mount(&sd_fs, "sd:", 1);
  printf("SD: mount result=%d\n", result);
  if (result != FR_OK) {
    return false;
  }
  sd_ready = true;
  printf("SD: ready at 25 MHz\n");
  return true;
}

static bool
read_file_resolved(const char *path, uint8_t *data, size_t capacity, size_t *size)
{
  FIL file;
  UINT bytes_read;
  size_t total = 0;
  FRESULT result = FR_OK;

  if (!sd_ready || path == NULL || size == NULL ||
      (capacity > 0 && data == NULL)) {
    printf("SD: read rejected path=%s\n", path != NULL ? path : "(null)");
    return false;
  }
  result = f_open(&file, path, FA_READ);
  if (result != FR_OK) {
    printf("SD: f_open path=%s result=%d\n", path, result);
    return false;
  }

  while (total < capacity) {
    UINT request = (UINT)((capacity - total) > 16 * 1024
                              ? 16 * 1024
                              : (capacity - total));
    if (request == 0) {
      break;
    }
    result = f_read(&file, data + total, request, &bytes_read);
    total += bytes_read;
    if (result != FR_OK || bytes_read == 0) {
      printf("SD: f_read path=%s result=%d bytes=%u total=%lu\n",
             path, result, (unsigned)bytes_read, (unsigned long)total);
      break;
    }
  }

  if (f_close(&file) != FR_OK || result != FR_OK) {
    printf("SD: f_close/read failed path=%s result=%d total=%lu expected=%lu\n",
           path, result, (unsigned long)total, (unsigned long)capacity);
    return false;
  }
  *size = total;
  printf("SD: read path=%s bytes=%lu\n", path, (unsigned long)total);
  return total == capacity;
}

static bool
resolve_asset_path(const char *asset, char *resolved, size_t resolved_size)
{
  if (asset == NULL || asset[0] == '\0' || asset[0] == '/' ||
      strstr(asset, "..") != NULL || strstr(asset, ":") != NULL) {
    return false;
  }
  int written = snprintf(resolved, resolved_size, "sd:/assets/%s", asset);
  return written > 0 && (size_t)written < resolved_size;
}

static bool
save_slot_valid(const char *slot)
{
  size_t length = 0;

  if (slot == NULL || slot[0] == '\0') {
    return false;
  }
  while (slot[length] != '\0') {
    char character = slot[length];
    bool alphanumeric = (character >= 'A' && character <= 'Z') ||
                        (character >= 'a' && character <= 'z') ||
                        (character >= '0' && character <= '9');
    if ((!alphanumeric && character != '_' && character != '-') ||
        length >= GAME_SAVE_SLOT_MAX) {
      return false;
    }
    length++;
  }
  return length > 0;
}

static bool
resolve_save_path(const char *slot, const char *suffix, char *resolved,
                  size_t resolved_size)
{
  int written;

  if (!save_slot_valid(slot) || suffix == NULL) {
    return false;
  }
  written = snprintf(resolved, resolved_size, "sd:/saves/%s.%s",
                     slot, suffix);
  return written > 0 && (size_t)written < resolved_size;
}

static bool
save_stat_path(const char *slot, char *resolved, size_t resolved_size,
               size_t *size)
{
  char target[128];
  char backup[128];
  FILINFO info;
  FRESULT result;

  if (!sd_ready || size == NULL ||
      !resolve_save_path(slot, "sav", target, sizeof(target)) ||
      !resolve_save_path(slot, "bak", backup, sizeof(backup))) {
    return false;
  }

  result = f_stat(target, &info);
  if (result != FR_OK && result != FR_NO_FILE && result != FR_NO_PATH) {
    printf("SD: save stat path=%s result=%d\n", target, result);
    return false;
  }
  if (result == FR_OK) {
    if (resolved != NULL) {
      strncpy(resolved, target, resolved_size);
      resolved[resolved_size - 1] = '\0';
    }
    *size = (size_t)info.fsize;
    return true;
  }

  result = f_stat(backup, &info);
  if (result != FR_OK) {
    if (result != FR_NO_FILE && result != FR_NO_PATH) {
      printf("SD: save backup stat path=%s result=%d\n", backup, result);
    }
    return false;
  }
  if (resolved != NULL) {
    strncpy(resolved, backup, resolved_size);
    resolved[resolved_size - 1] = '\0';
  }
  *size = (size_t)info.fsize;
  return true;
}

static bool
save_path_missing(FRESULT result)
{
  return result == FR_NO_FILE || result == FR_NO_PATH;
}

static bool
save_directory_ready(void)
{
  FRESULT result;

  result = f_mkdir("sd:/saves");
  if (result != FR_OK && result != FR_EXIST) {
    printf("SD: save directory create failed result=%d\n", result);
    return false;
  }
  return true;
}

static bool
save_unlink_if_present(const char *path)
{
  FRESULT result = f_unlink(path);

  if (result == FR_OK || result == FR_NO_FILE || result == FR_NO_PATH) {
    return true;
  }
  printf("SD: save unlink path=%s result=%d\n", path, result);
  return false;
}

static bool
save_recover_backup(const char *target, const char *backup)
{
  FILINFO info;
  FRESULT target_result = f_stat(target, &info);
  FRESULT backup_result = f_stat(backup, &info);

  if (!save_path_missing(target_result)) {
    if (target_result != FR_OK) {
      printf("SD: save target stat path=%s result=%d\n", target,
             target_result);
      return false;
    }
    /* A backup left by an earlier completed replacement is stale. */
    return save_unlink_if_present(backup);
  }
  if (backup_result == FR_OK) {
    FRESULT result = f_rename(backup, target);
    if (result != FR_OK) {
      printf("SD: save backup recovery failed result=%d\n", result);
      return false;
    }
    return true;
  }
  if (!save_path_missing(backup_result)) {
    printf("SD: save backup stat path=%s result=%d\n", backup, backup_result);
    return false;
  }
  return true;
}

static bool
save_write_file(const char *path, const uint8_t *data, size_t size)
{
  FIL file;
  FRESULT result;
  size_t total = 0;

  result = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK) {
    printf("SD: save open path=%s result=%d\n", path, result);
    return false;
  }

  while (total < size) {
    UINT request = (UINT)((size - total) > 16 * 1024
                              ? 16 * 1024
                              : (size - total));
    UINT bytes_written = 0;
    result = f_write(&file, data + total, request, &bytes_written);
    total += bytes_written;
    if (result != FR_OK || bytes_written != request) {
      printf("SD: save write path=%s result=%d bytes=%u total=%lu\n",
             path, result, (unsigned)bytes_written, (unsigned long)total);
      break;
    }
  }
  if (result == FR_OK) {
    result = f_sync(&file);
  }
  if (f_close(&file) != FR_OK || result != FR_OK || total != size) {
    printf("SD: save close failed path=%s result=%d total=%lu expected=%lu\n",
           path, result, (unsigned long)total, (unsigned long)size);
    return false;
  }
  return true;
}

void *
game_vm_heap(void)
{
  return psram_base;
}

size_t
game_vm_heap_size(void)
{
  return GAME_PSRAM_VM_HEAP_SIZE;
}

bool
game_platform_boot(void)
{
  size_t psram_size;
  uint32_t splash_started;

  if (platform_ready) {
    return true;
  }
  psram_size = psram_get_size();
  printf("PSRAM: available=%d size=%lu bytes\n", psram_is_available(),
         (unsigned long)psram_size);
  if (!psram_is_available() || psram_size < GAME_PSRAM_TOTAL_BYTES) {
    printf("PSRAM: unavailable or smaller than expected\n");
    return false;
  }

  volatile uint32_t *first = (volatile uint32_t *)psram_base;
  volatile uint32_t *last = (volatile uint32_t *)(psram_base + GAME_PSRAM_TOTAL_BYTES - 4);
  *first = 0x5049434fu;
  *last = 0x52554259u;
  if (*first != 0x5049434fu || *last != 0x52554259u) {
    printf("PSRAM: pattern test failed\n");
    return false;
  }
  printf("PSRAM: pattern test OK\n");
  printf("SPRITE CACHE: fixed SRAM capacity=%u bytes\n",
         (unsigned)GAME_SPRITE_CACHE_BYTES);
#if GAME_ENABLE_FRAME_DIFF
  frame_diff_previous = (uint8_t *)media_alloc(GAME_FRAMEBUFFER_BYTES, 4u);
  if (frame_diff_previous == NULL) {
    printf("FRAME DIFF: shadow buffer allocation failed; using draw dirty regions\n");
    frame_diff_ready = false;
  } else {
    frame_diff_ready = true;
    frame_diff_valid = false;
    printf("FRAME DIFF: PSRAM shadow buffer ready bytes=%u\n",
           (unsigned)GAME_FRAMEBUFFER_BYTES);
  }
#endif

#if GAME_TFT_CORE1
  game_3d_depth_buffer = (uint16_t *)media_alloc(GAME_3D_DEPTH_BUFFER_BYTES,
                                                 4u);
#if GAME_3D_DEPTH_SRAM
  game_3d_depth_psram = game_3d_depth_buffer;
  printf("3D: low depth SRAM reserved bytes=%u\n",
         (unsigned)sizeof(game_3d_depth_low_storage));
#endif
  if (game_3d_depth_buffer == NULL) {
    printf("3D: depth buffer allocation failed; capability disabled\n");
  } else {
    printf("3D: depth buffer ready PSRAM bytes=%u\n",
           (unsigned)GAME_3D_DEPTH_BUFFER_BYTES);
  }
#else
  game_3d_depth_buffer = game_3d_depth_storage;
  printf("3D: depth buffer ready SRAM bytes=%u\n",
         (unsigned)GAME_3D_DEPTH_BUFFER_BYTES);
#endif
  game_3d_select_depth_buffer();
  game_3d_indexed_vertices = (struct game_3d_fixed_vertex *)media_alloc(
    sizeof(*game_3d_indexed_vertices) * GAME_3D_INDEXED_MAX_VERTICES, 8u);
  if (game_3d_indexed_vertices == NULL) {
    printf("3D: indexed mesh scratch allocation failed\n");
  }
  printf("3D: optimizations clip=%d projection_cache=%d depth_sram=%d model_shared=%d triangle=%d\n",
         GAME_3D_LINE_CLIP, GAME_3D_PROJECTION_CACHE,
         GAME_TFT_CORE1 && GAME_3D_DEPTH_SRAM, GAME_3D_MODEL_SHARED,
         GAME_3D_TRIANGLE_MODE);
#if GAME_3D_PROJECTION_CACHE
  game_3d_projections = media_alloc(sizeof(*game_3d_projections) *
                                    GAME_3D_INDEXED_MAX_VERTICES, 8u);
  if (game_3d_projections != NULL) {
    memset(game_3d_projections, 0, sizeof(*game_3d_projections) *
                                    GAME_3D_INDEXED_MAX_VERTICES);
    printf("3D: projection scratch ready PSRAM bytes=%u\n",
           (unsigned)(sizeof(*game_3d_projections) * GAME_3D_INDEXED_MAX_VERTICES));
  } else {
    printf("3D: projection scratch unavailable; using uncached path\n");
  }
#endif

  input_init();
  printf("INPUT: GP2..GP7, GP9, GP12, GP14, GP16 pull-ups enabled\n");
  tft_init();
  if (!game_display_boot()) {
    printf("TFT: initial frame transfer failed\n");
    return false;
  }
  splash_started = time_us_32();
  printf("TFT: initial frame transferred\n");

  audio_init();
  audio_boot_chime();

  if (!sd_init()) {
    printf("SD initialization failed\n");
    platform_boot_error = "CARTRIDGE";
    platform_ready = tft_ready;
    return false;
  }

  uint32_t splash_remaining = game_boot_screen_remaining_ms(
    (uint32_t)(time_us_32() - splash_started));
  if (splash_remaining > 0) sleep_ms(splash_remaining);

#if GAME_ENABLE_PROFILING
  /* Do not include the boot splash and SD/asset setup in the first report. */
  profile_reset();
#endif
  platform_ready = true;
  return true;
}

const char *
game_platform_boot_error(void)
{
  return platform_boot_error;
}

static uint32_t
input_poll_state(void)
{
  uint32_t raw = 0;
  uint32_t now = time_us_32();
  for (size_t i = 0; i < INPUT_COUNT; i++) {
    if (!gpio_get(button_pins[i])) {
      raw |= 1u << i;
    }
  }

  uint32_t stable = input_stable;
  for (size_t i = 0; i < INPUT_COUNT; i++) {
    uint32_t mask = 1u << i;
    if ((raw ^ input_candidate) & mask) {
      input_candidate = (input_candidate & ~mask) | (raw & mask);
      input_changed_at[i] = now;
    } else if ((uint32_t)(now - input_changed_at[i]) >= INPUT_DEBOUNCE_US) {
      stable = (stable & ~mask) | (input_candidate & mask);
    }
  }
  /* Publish one complete snapshot to the frame loop on Core 0. */
  input_stable = stable;
  return input_stable;
}

uint32_t
game_input_state(void)
{
  return input_frame_state;
}

void
game_input_begin_frame(void)
{
  if (!input_timer_ready) { game_audio_dial_poll(); game_battery_poll(); }
  input_previous_state = input_frame_state;
  input_frame_state = input_timer_ready ? input_stable : input_poll_state();
}

bool
game_input_pressed(uint32_t button)
{
  return (game_input_state() & button) != 0;
}

bool
game_input_just_pressed(uint32_t button)
{
  return (input_frame_state & button) != 0 &&
         (input_previous_state & button) == 0;
}

bool
game_input_just_released(uint32_t button)
{
  return (input_frame_state & button) == 0 &&
         (input_previous_state & button) != 0;
}

void
game_time_reset(void)
{
  frame_time_previous_us = time_us_32();
  frame_delta_ms = 0;
}

void
game_time_begin_frame(void)
{
  uint32_t now = time_us_32();

  frame_delta_ms = (uint32_t)(now - frame_time_previous_us) / 1000u;
  frame_time_previous_us = now;
}

uint32_t
game_time_delta_ms(void)
{
  return frame_delta_ms;
}

void
game_display_clear(uint16_t color)
{
  uint8_t hi = (uint8_t)(color >> 8);
  uint8_t lo = (uint8_t)color;
  /* RGB565 repeats a 16-bit value; account for little-endian memory order. */
  uint32_t pattern = ((uint32_t)lo << 24) | ((uint32_t)hi << 16) |
                     ((uint32_t)lo << 8) | hi;
  uint32_t *destination = (uint32_t *)(void *)framebuffer;
#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif
  for (size_t i = 0; i < GAME_FRAMEBUFFER_BYTES / sizeof(*destination); i++) {
    destination[i] = pattern;
  }
  display_dirty_add(0, 0, GAME_DISPLAY_WIDTH, GAME_DISPLAY_HEIGHT);
#if GAME_ENABLE_PROFILING
  profile_add(&profile_clear_us, profile_started);
#endif
}

bool
game_3d_supported(void)
{
  return tft_ready && game_3d_depth_buffer != NULL;
}

bool
game_3d_begin_frame(void)
{
  size_t depth_bytes;

  if (!game_3d_supported()) {
    game_3d_frame_active = false;
    return false;
  }
  depth_bytes = (size_t)game_3d_internal_width *
                (size_t)game_3d_internal_height * sizeof(uint16_t);
  memset(game_3d_depth_buffer, 0, depth_bytes);
  game_3d_frame_active = true;
  return true;
}

bool
game_3d_set_viewport(int x, int y, int width, int height)
{
  if (game_3d_frame_active || x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x > (int)GAME_DISPLAY_WIDTH - width ||
      y > (int)GAME_DISPLAY_HEIGHT - height) {
    return false;
  }
  game_3d_viewport_x = (uint16_t)x;
  game_3d_viewport_y = (uint16_t)y;
  game_3d_viewport_width = (uint16_t)width;
  game_3d_viewport_height = (uint16_t)height;
  game_3d_update_scale_mode();
  return true;
}

static bool
game_3d_update_focal_length(void)
{
  const float degrees_to_radians = 0.017453292519943295f;
  float half_fov = game_3d_camera.fov_degrees * degrees_to_radians * 0.5f;
  float tangent = tanf(half_fov);

  if (!isfinite(tangent) || tangent <= 0.0f) {
    return false;
  }
  game_3d_camera.focal_length =
    ((float)game_3d_internal_width * 0.5f) / tangent;
  game_3d_camera.focal_length_fixed =
    (int64_t)lrintf(game_3d_camera.focal_length * (float)GAME_3D_FIXED_ONE);
  return isfinite(game_3d_camera.focal_length);
}

static int64_t
game_3d_fixed_from_float(float value)
{
  return (int64_t)lrintf(value * (float)GAME_3D_FIXED_ONE);
}

static bool
game_3d_fixed_value_supported(float value)
{
  return isfinite(value) && fabsf(value) <= 32768.0f;
}

static int64_t
game_3d_fixed_multiply(int64_t left, int64_t right)
{
  int64_t product = left * right;

  if (product >= 0) {
    return (product + GAME_3D_FIXED_HALF) >> GAME_3D_FIXED_SHIFT;
  }
  return -(((-product) + GAME_3D_FIXED_HALF) >> GAME_3D_FIXED_SHIFT);
}

bool
game_3d_set_resolution(int width, int height)
{
  uint16_t previous_width;
  uint16_t previous_height;

  if (game_3d_frame_active || width <= 0 || height <= 0 ||
      width > (int)GAME_DISPLAY_WIDTH || height > (int)GAME_DISPLAY_HEIGHT) {
    return false;
  }
  previous_width = game_3d_internal_width;
  previous_height = game_3d_internal_height;
  game_3d_internal_width = (uint16_t)width;
  game_3d_internal_height = (uint16_t)height;
  if (!game_3d_update_focal_length()) {
    game_3d_internal_width = previous_width;
    game_3d_internal_height = previous_height;
    game_3d_update_focal_length();
    return false;
  }
  game_3d_update_scale_mode();
  game_3d_select_depth_buffer();
  return true;
}

bool
game_3d_set_camera(float x, float y, float z, float yaw, float pitch,
                   float fov_degrees, float near_clip, float far_clip)
{
  const float degrees_to_radians = 0.017453292519943295f;
  float half_fov;
  float tangent;
  float focal_length;
  float near_inverse;
  float far_inverse;
  float inverse_range;

  if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(yaw) ||
      !isfinite(pitch) || !isfinite(fov_degrees) || !isfinite(near_clip) ||
      !isfinite(far_clip) || fov_degrees <= 1.0f || fov_degrees >= 179.0f ||
      near_clip <= 0.0f || far_clip <= near_clip) {
    return false;
  }

  half_fov = fov_degrees * degrees_to_radians * 0.5f;
  tangent = tanf(half_fov);
  focal_length = ((float)game_3d_internal_width * 0.5f) / tangent;
  near_inverse = 1.0f / near_clip;
  far_inverse = 1.0f / far_clip;
  inverse_range = 1.0f / (near_inverse - far_inverse);
  if (!isfinite(tangent) || tangent <= 0.0f || !isfinite(focal_length) ||
      !isfinite(near_inverse) || !isfinite(far_inverse) ||
      !isfinite(inverse_range)) {
    return false;
  }

  game_3d_camera.x = x;
  game_3d_camera.y = y;
  game_3d_camera.z = z;
  game_3d_camera.yaw = yaw;
  game_3d_camera.pitch = pitch;
  game_3d_camera.fov_degrees = fov_degrees;
  game_3d_camera.near_clip = near_clip;
  game_3d_camera.far_clip = far_clip;
  game_3d_camera.focal_length = focal_length;
  game_3d_camera.focal_length_fixed =
    (int64_t)lrintf(focal_length * (float)GAME_3D_FIXED_ONE);
  game_3d_camera.sin_yaw = sinf(yaw);
  game_3d_camera.cos_yaw = cosf(yaw);
  game_3d_camera.sin_pitch = sinf(pitch);
  game_3d_camera.cos_pitch = cosf(pitch);
  game_3d_camera.depth_scale = inverse_range;
  game_3d_camera.depth_bias = -far_inverse * inverse_range;
  game_3d_camera.depth_scale_fixed =
    (int32_t)lrintf(inverse_range * (float)GAME_3D_FIXED_ONE);
  game_3d_camera.depth_bias_fixed =
    (int32_t)lrintf(game_3d_camera.depth_bias * (float)GAME_3D_FIXED_ONE);
  game_3d_camera.x_fixed = game_3d_fixed_from_float(x);
  game_3d_camera.y_fixed = game_3d_fixed_from_float(y);
  game_3d_camera.z_fixed = game_3d_fixed_from_float(z);
  game_3d_camera.sin_yaw_fixed = game_3d_fixed_from_float(
    game_3d_camera.sin_yaw);
  game_3d_camera.cos_yaw_fixed = game_3d_fixed_from_float(
    game_3d_camera.cos_yaw);
  game_3d_camera.sin_pitch_fixed = game_3d_fixed_from_float(
    game_3d_camera.sin_pitch);
  game_3d_camera.cos_pitch_fixed = game_3d_fixed_from_float(
    game_3d_camera.cos_pitch);
  return isfinite(game_3d_camera.sin_yaw) &&
         isfinite(game_3d_camera.cos_yaw) &&
         isfinite(game_3d_camera.sin_pitch) &&
         isfinite(game_3d_camera.cos_pitch) &&
         isfinite(game_3d_camera.depth_scale) &&
         isfinite(game_3d_camera.depth_bias) &&
         game_3d_camera.depth_scale_fixed > 0;
}

static struct game_3d_vertex
game_3d_transform_vertex(float x, float y, float z)
{
  float relative_x = x - game_3d_camera.x;
  float relative_y = y - game_3d_camera.y;
  float relative_z = z - game_3d_camera.z;
  struct game_3d_vertex transformed;

  /* The camera looks along +Z. Yaw is a rotation around +Y and pitch is a
   * rotation around the camera's local X axis. */
  transformed.x = relative_x * game_3d_camera.cos_yaw -
                  relative_z * game_3d_camera.sin_yaw;
  transformed.y = -relative_x * game_3d_camera.sin_yaw *
                    game_3d_camera.sin_pitch +
                  relative_y * game_3d_camera.cos_pitch -
                  relative_z * game_3d_camera.cos_yaw *
                    game_3d_camera.sin_pitch;
  transformed.z = relative_x * game_3d_camera.cos_pitch *
                    game_3d_camera.sin_yaw +
                  relative_y * game_3d_camera.sin_pitch +
                  relative_z * game_3d_camera.cos_pitch *
                    game_3d_camera.cos_yaw;
  return transformed;
}

static struct game_3d_fixed_vertex
game_3d_transform_vertex_fixed(float x, float y, float z)
{
  int64_t relative_x = game_3d_fixed_from_float(x) -
                       game_3d_camera.x_fixed;
  int64_t relative_y = game_3d_fixed_from_float(y) -
                       game_3d_camera.y_fixed;
  int64_t relative_z = game_3d_fixed_from_float(z) -
                       game_3d_camera.z_fixed;
  struct game_3d_fixed_vertex transformed;

  transformed.x = game_3d_fixed_multiply(relative_x,
                                         game_3d_camera.cos_yaw_fixed) -
                  game_3d_fixed_multiply(relative_z,
                                         game_3d_camera.sin_yaw_fixed);
  transformed.y = -game_3d_fixed_multiply(
                    game_3d_fixed_multiply(relative_x,
                                           game_3d_camera.sin_yaw_fixed),
                    game_3d_camera.sin_pitch_fixed) +
                  game_3d_fixed_multiply(relative_y,
                                         game_3d_camera.cos_pitch_fixed) -
                  game_3d_fixed_multiply(
                    game_3d_fixed_multiply(relative_z,
                                           game_3d_camera.cos_yaw_fixed),
                    game_3d_camera.sin_pitch_fixed);
  transformed.z = game_3d_fixed_multiply(
                    game_3d_fixed_multiply(relative_x,
                                           game_3d_camera.sin_yaw_fixed),
                    game_3d_camera.cos_pitch_fixed) +
                  game_3d_fixed_multiply(relative_y,
                                         game_3d_camera.sin_pitch_fixed) +
                  game_3d_fixed_multiply(
                    game_3d_fixed_multiply(relative_z,
                                           game_3d_camera.cos_yaw_fixed),
                    game_3d_camera.cos_pitch_fixed);
  return transformed;
}

static struct game_3d_vertex
game_3d_vertex_from_fixed(const struct game_3d_fixed_vertex *vertex)
{
  struct game_3d_vertex converted;
  converted.x = (float)vertex->x / (float)GAME_3D_FIXED_ONE;
  converted.y = (float)vertex->y / (float)GAME_3D_FIXED_ONE;
  converted.z = (float)vertex->z / (float)GAME_3D_FIXED_ONE;
  return converted;
}

static bool
game_3d_vertex_inside_z(const struct game_3d_vertex *vertex, float plane,
                        bool keep_greater)
{
  return keep_greater ? vertex->z >= plane : vertex->z <= plane;
}

static int
game_3d_clip_z(const struct game_3d_vertex *input, int input_count,
               float plane, bool keep_greater,
               struct game_3d_vertex *output)
{
  int output_count = 0;

  for (int index = 0; index < input_count; index++) {
    const struct game_3d_vertex *current = &input[index];
    const struct game_3d_vertex *next = &input[(index + 1) % input_count];
    bool current_inside = game_3d_vertex_inside_z(current, plane,
                                                   keep_greater);
    bool next_inside = game_3d_vertex_inside_z(next, plane, keep_greater);

    if (current_inside != next_inside) {
      float denominator = next->z - current->z;
      float amount = denominator == 0.0f
        ? 0.0f : (plane - current->z) / denominator;
      struct game_3d_vertex intersection;
      intersection.x = current->x + (next->x - current->x) * amount;
      intersection.y = current->y + (next->y - current->y) * amount;
      intersection.z = plane;
      if (output_count < 8) output[output_count++] = intersection;
    }
    if (next_inside && output_count < 8) {
      output[output_count++] = *next;
    }
  }
  return output_count;
}

static float
game_3d_projected_area_numerator(const struct game_3d_vertex *first,
                                 const struct game_3d_vertex *second,
                                 const struct game_3d_vertex *third)
{
  float delta_x12 = second->x * first->z - first->x * second->z;
  float delta_y12 = second->y * first->z - first->y * second->z;
  float delta_x13 = third->x * first->z - first->x * third->z;
  float delta_y13 = third->y * first->z - first->y * third->z;

  /* This is the signed screen-space area with the positive focal-length and
   * Y-down conventions used by the rasterizer, with positive denominators
   * omitted. Avoiding the divisions lets back-facing triangles exit before
   * clipping and perspective projection. */
  return delta_y13 * delta_x12 - delta_x13 * delta_y12;
}

static int64_t game_3d_fixed_coordinate(float value);
static int32_t game_3d_fixed_inverse_z(float inverse_z);

static struct game_3d_projected_vertex
game_3d_project_vertex(const struct game_3d_vertex *vertex)
{
  struct game_3d_projected_vertex projected;
  projected.x = (float)game_3d_internal_width * 0.5f +
                vertex->x * game_3d_camera.focal_length / vertex->z;
  projected.y = (float)game_3d_internal_height * 0.5f -
                vertex->y * game_3d_camera.focal_length / vertex->z;
  projected.inverse_z = 1.0f / vertex->z;
  projected.x_fixed = game_3d_fixed_coordinate(projected.x);
  projected.y_fixed = game_3d_fixed_coordinate(projected.y);
  projected.inverse_z_fixed = game_3d_fixed_inverse_z(projected.inverse_z);
  return projected;
}

static float
game_3d_edge(float ax, float ay, float bx, float by, float px, float py)
{
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static int64_t
game_3d_fixed_coordinate(float value)
{
  const int64_t limit = (int64_t)(GAME_3D_FIXED_COORD_LIMIT *
                                  (float)GAME_3D_FIXED_ONE);

  if (value >= GAME_3D_FIXED_COORD_LIMIT) return limit;
  if (value <= -GAME_3D_FIXED_COORD_LIMIT) return -limit;
  return (int64_t)lrintf(value * (float)GAME_3D_FIXED_ONE);
}

static int64_t
game_3d_fixed_edge(int64_t ax, int64_t ay, int64_t bx, int64_t by,
                   int64_t px, int64_t py)
{
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static int32_t
game_3d_fixed_inverse_z(float inverse_z)
{
  return (int32_t)lrintf(inverse_z * (float)GAME_3D_FIXED_ONE);
}

static struct game_3d_projected_vertex
game_3d_project_fixed_vertex(const struct game_3d_fixed_vertex *vertex)
{
  int64_t projected_x = (vertex->x * game_3d_camera.focal_length_fixed) /
                        vertex->z;
  int64_t projected_y = (vertex->y * game_3d_camera.focal_length_fixed) /
                        vertex->z;
  int64_t inverse_z = ((int64_t)GAME_3D_FIXED_ONE *
                       (int64_t)GAME_3D_FIXED_ONE + vertex->z / 2) /
                      vertex->z;
  struct game_3d_projected_vertex projected;

  projected.x_fixed = ((int64_t)game_3d_internal_width *
                       GAME_3D_FIXED_ONE) / 2 + projected_x;
  projected.y_fixed = ((int64_t)game_3d_internal_height *
                       GAME_3D_FIXED_ONE) / 2 - projected_y;
  projected.inverse_z_fixed = (int32_t)inverse_z;
  projected.x = (float)projected.x_fixed / (float)GAME_3D_FIXED_ONE;
  projected.y = (float)projected.y_fixed / (float)GAME_3D_FIXED_ONE;
  projected.inverse_z = (float)projected.inverse_z_fixed /
                        (float)GAME_3D_FIXED_ONE;
  return projected;
}


/* Epochs limit reuse to one indexed draw, including calls made by Model. */
static void
game_3d_projection_begin(void)
{
#if GAME_3D_PROJECTION_CACHE
  if (game_3d_projections && ++game_3d_projection_generation == 0) {
    memset(game_3d_projections, 0, sizeof(*game_3d_projections) *
                                    GAME_3D_INDEXED_MAX_VERTICES);
    game_3d_projection_generation = 1;
  }
#endif
}

static struct game_3d_projected_vertex
game_3d_project_shared_vertex(const struct game_3d_fixed_vertex *vertex)
{
#if GAME_3D_PROJECTION_CACHE
  uintptr_t offset = (uintptr_t)vertex - (uintptr_t)game_3d_indexed_vertices;
  if (game_3d_projections && game_3d_projection_generation != 0 &&
      offset < sizeof(*vertex) * GAME_3D_INDEXED_MAX_VERTICES &&
      offset % sizeof(*vertex) == 0) {
    struct game_3d_projection_entry *entry =
      &game_3d_projections[offset / sizeof(*vertex)];
    if (entry->generation != game_3d_projection_generation) {
      entry->value = game_3d_project_fixed_vertex(vertex);
      entry->generation = game_3d_projection_generation;
    }
    return entry->value;
  }
#endif
  return game_3d_project_fixed_vertex(vertex);
}

static uint16_t
game_3d_depth_value_fixed(int32_t inverse_z)
{
  int64_t normalized = ((int64_t)inverse_z *
                       game_3d_camera.depth_scale_fixed) >>
                      GAME_3D_FIXED_SHIFT;
  int64_t depth;

  normalized += game_3d_camera.depth_bias_fixed;
  if (normalized <= 0) return 0;
  if (normalized >= GAME_3D_FIXED_ONE) return UINT16_MAX;
  depth = (normalized * (int64_t)UINT16_MAX + GAME_3D_FIXED_HALF) >>
          GAME_3D_FIXED_SHIFT;
  return (uint16_t)depth;
}

static void
game_3d_add_dirty_rect(int left, int top, int right, int bottom)
{
  int output_left = (int)game_3d_viewport_x +
                    (left * (int)game_3d_viewport_width) /
                    (int)game_3d_internal_width;
  int output_top = (int)game_3d_viewport_y +
                   (top * (int)game_3d_viewport_height) /
                   (int)game_3d_internal_height;
  int output_right = (int)game_3d_viewport_x +
                     (((right + 1) * (int)game_3d_viewport_width +
                       (int)game_3d_internal_width - 1) /
                      (int)game_3d_internal_width) - 1;
  int output_bottom = (int)game_3d_viewport_y +
                      (((bottom + 1) * (int)game_3d_viewport_height +
                        (int)game_3d_internal_height - 1) /
                       (int)game_3d_internal_height) - 1;

  display_dirty_add(output_left, output_top,
                    output_right - output_left + 1,
                    output_bottom - output_top + 1);
}

static void
game_3d_write_pixel(int x, int y, uint8_t high, uint8_t low)
{
  if (game_3d_internal_width == game_3d_viewport_width &&
      game_3d_internal_height == game_3d_viewport_height) {
    size_t offset = (((size_t)game_3d_viewport_y + (size_t)y) *
                     GAME_DISPLAY_WIDTH +
                     (size_t)game_3d_viewport_x + (size_t)x) * 2u;
    framebuffer[offset] = high;
    framebuffer[offset + 1u] = low;
    return;
  }

  if (game_3d_scale_2x) {
    int output_x = (int)game_3d_viewport_x + (x << 1);
    int output_y = (int)game_3d_viewport_y + (y << 1);
    size_t offset = ((size_t)output_y * GAME_DISPLAY_WIDTH +
                     (size_t)output_x) * 2u;

    framebuffer[offset] = high;
    framebuffer[offset + 1u] = low;
    framebuffer[offset + 2u] = high;
    framebuffer[offset + 3u] = low;
    offset += (size_t)GAME_DISPLAY_WIDTH * 2u;
    framebuffer[offset] = high;
    framebuffer[offset + 1u] = low;
    framebuffer[offset + 2u] = high;
    framebuffer[offset + 3u] = low;
    return;
  }

  {
    int left = (int)game_3d_viewport_x +
               (x * (int)game_3d_viewport_width) /
               (int)game_3d_internal_width;
    int top = (int)game_3d_viewport_y +
              (y * (int)game_3d_viewport_height) /
              (int)game_3d_internal_height;
    int right = (int)game_3d_viewport_x +
                (((x + 1) * (int)game_3d_viewport_width +
                  (int)game_3d_internal_width - 1) /
                 (int)game_3d_internal_width) - 1;
    int bottom = (int)game_3d_viewport_y +
                 (((y + 1) * (int)game_3d_viewport_height +
                   (int)game_3d_internal_height - 1) /
                  (int)game_3d_internal_height) - 1;

    for (int output_y = top; output_y <= bottom; output_y++) {
      size_t row_offset = (size_t)output_y * GAME_DISPLAY_WIDTH * 2u;
      for (int output_x = left; output_x <= right; output_x++) {
        size_t offset = row_offset + (size_t)output_x * 2u;
        framebuffer[offset] = high;
        framebuffer[offset + 1u] = low;
      }
    }
  }
}

#if GAME_3D_TRIANGLE_MODE == 2
/* Intersect a row with E(x) >= 0. Called only inside the +/-4096 scan
 * bound, so the 16.16 products and bounds fit int64_t. */
static bool
game_3d_triangle_span(int64_t edge, int64_t step, int *begin, int *end)
{
  if (step > 0 && edge < 0) {
    int64_t first = (-edge + step - 1) / step;
    if (first > *end) return false;
    if (first > *begin) *begin = (int)first;
  } else if (step < 0) {
    if (edge < 0) return false;
    int64_t last = edge / -step;
    if (last < *begin) return false;
    if (last < *end) *end = (int)last;
  } else if (step == 0 && edge < 0) {
    return false;
  }
  return true;
}
#endif

static void
game_3d_raster_triangle(const struct game_3d_projected_vertex *first,
                        const struct game_3d_projected_vertex *second,
                        const struct game_3d_projected_vertex *third,
                        uint16_t color)
{
  float area = game_3d_edge(first->x, first->y, second->x, second->y,
                            third->x, third->y);
  float min_x = first->x < second->x ? first->x : second->x;
  float max_x = first->x > second->x ? first->x : second->x;
  float min_y = first->y < second->y ? first->y : second->y;
  float max_y = first->y > second->y ? first->y : second->y;
  int left;
  int right;
  int top;
  int bottom;
  uint8_t high = (uint8_t)(color >> 8);
  uint8_t low = (uint8_t)color;
  int64_t first_x_fixed;
  int64_t first_y_fixed;
  int64_t second_x_fixed;
  int64_t second_y_fixed;
  int64_t third_x_fixed;
  int64_t third_y_fixed;
  int64_t fixed_area;
  int64_t edge0_step_x;
  int64_t edge1_step_x;
  int64_t edge2_step_x;
  float inverse_area;
  float inverse_z_at_start;
  float inverse_z_step_x;
  float inverse_z_step_y;
  int32_t inverse_z_row;
  int32_t inverse_z_step_x_fixed;
  int32_t inverse_z_step_y_fixed;

  if (third->x < min_x) min_x = third->x;
  if (third->x > max_x) max_x = third->x;
  if (third->y < min_y) min_y = third->y;
  if (third->y > max_y) max_y = third->y;

  /* The projected coordinate system has Y down, so positive area is the
   * front-facing winding used by the Game API. */
  if (!isfinite(area) || area <= 0.000001f ||
      !isfinite(min_x) || !isfinite(max_x) ||
      !isfinite(min_y) || !isfinite(max_y) ||
      max_x < 0.0f || max_y < 0.0f ||
      min_x >= (float)game_3d_internal_width ||
      min_y >= (float)game_3d_internal_height) {
    return;
  }
  left = min_x <= 0.0f ? 0 : (int)floorf(min_x);
  right = max_x >= (float)game_3d_internal_width
    ? (int)game_3d_internal_width - 1 : (int)ceilf(max_x) - 1;
  top = min_y <= 0.0f ? 0 : (int)floorf(min_y);
  bottom = max_y >= (float)game_3d_internal_height
    ? (int)game_3d_internal_height - 1 : (int)ceilf(max_y) - 1;
  if (left > right || top > bottom) return;

  first_x_fixed = first->x_fixed;
  first_y_fixed = first->y_fixed;
  second_x_fixed = second->x_fixed;
  second_y_fixed = second->y_fixed;
  third_x_fixed = third->x_fixed;
  third_y_fixed = third->y_fixed;
  fixed_area = game_3d_fixed_edge(first_x_fixed, first_y_fixed,
                                  second_x_fixed, second_y_fixed,
                                  third_x_fixed, third_y_fixed);
  if (fixed_area <= 0) return;

  inverse_area = 1.0f / area;
  inverse_z_at_start =
    (game_3d_edge(second->x, second->y, third->x, third->y,
                  (float)left + 0.5f, (float)top + 0.5f) * first->inverse_z +
     game_3d_edge(third->x, third->y, first->x, first->y,
                  (float)left + 0.5f, (float)top + 0.5f) * second->inverse_z +
     game_3d_edge(first->x, first->y, second->x, second->y,
                  (float)left + 0.5f, (float)top + 0.5f) * third->inverse_z) *
    inverse_area;
  inverse_z_step_x = ((third->y - second->y) * first->inverse_z +
                      (first->y - third->y) * second->inverse_z +
                      (second->y - first->y) * third->inverse_z) *
                     inverse_area;
  inverse_z_step_y = ((second->x - third->x) * first->inverse_z +
                      (third->x - first->x) * second->inverse_z +
                      (first->x - second->x) * third->inverse_z) *
                     inverse_area;
  inverse_z_row = game_3d_fixed_inverse_z(inverse_z_at_start);
  inverse_z_step_x_fixed = game_3d_fixed_inverse_z(inverse_z_step_x);
  inverse_z_step_y_fixed = game_3d_fixed_inverse_z(inverse_z_step_y);
  edge0_step_x = (third_y_fixed - second_y_fixed) * GAME_3D_FIXED_ONE;
  edge1_step_x = (first_y_fixed - third_y_fixed) * GAME_3D_FIXED_ONE;
  edge2_step_x = (second_y_fixed - first_y_fixed) * GAME_3D_FIXED_ONE;
#if GAME_3D_TRIANGLE_MODE != 0
  const int64_t scan_limit = INT64_C(4096) * GAME_3D_FIXED_ONE;
  bool triangle_fast_safe =
    first_x_fixed >= -scan_limit && first_x_fixed <= scan_limit &&
    first_y_fixed >= -scan_limit && first_y_fixed <= scan_limit &&
    second_x_fixed >= -scan_limit && second_x_fixed <= scan_limit &&
    second_y_fixed >= -scan_limit && second_y_fixed <= scan_limit &&
    third_x_fixed >= -scan_limit && third_x_fixed <= scan_limit &&
    third_y_fixed >= -scan_limit && third_y_fixed <= scan_limit;
  int64_t sample_x = ((int64_t)left << GAME_3D_FIXED_SHIFT) + GAME_3D_FIXED_HALF;
  int64_t sample_y = ((int64_t)top << GAME_3D_FIXED_SHIFT) + GAME_3D_FIXED_HALF;
  int64_t edge0_row = triangle_fast_safe ? game_3d_fixed_edge(second_x_fixed, second_y_fixed,
    third_x_fixed, third_y_fixed, sample_x, sample_y) : 0;
  int64_t edge1_row = triangle_fast_safe ? game_3d_fixed_edge(third_x_fixed, third_y_fixed,
    first_x_fixed, first_y_fixed, sample_x, sample_y) : 0;
  int64_t edge2_row = triangle_fast_safe ? game_3d_fixed_edge(first_x_fixed, first_y_fixed,
    second_x_fixed, second_y_fixed, sample_x, sample_y) : 0;
  int64_t edge0_step_y = triangle_fast_safe ? (second_x_fixed - third_x_fixed) * GAME_3D_FIXED_ONE : 0;
  int64_t edge1_step_y = triangle_fast_safe ? (third_x_fixed - first_x_fixed) * GAME_3D_FIXED_ONE : 0;
  int64_t edge2_step_y = triangle_fast_safe ? (first_x_fixed - second_x_fixed) * GAME_3D_FIXED_ONE : 0;
#endif
  game_3d_add_dirty_rect(left, top, right, bottom);
  for (int pixel_y = top; pixel_y <= bottom; pixel_y++) {
#if GAME_3D_TRIANGLE_MODE != 0
    int64_t row_sample_y = ((int64_t)pixel_y << GAME_3D_FIXED_SHIFT) + GAME_3D_FIXED_HALF;
    int64_t edge0 = triangle_fast_safe ? edge0_row : game_3d_fixed_edge(
      second_x_fixed, second_y_fixed, third_x_fixed, third_y_fixed, sample_x, row_sample_y);
    int64_t edge1 = triangle_fast_safe ? edge1_row : game_3d_fixed_edge(
      third_x_fixed, third_y_fixed, first_x_fixed, first_y_fixed, sample_x, row_sample_y);
    int64_t edge2 = triangle_fast_safe ? edge2_row : game_3d_fixed_edge(
      first_x_fixed, first_y_fixed, second_x_fixed, second_y_fixed, sample_x, row_sample_y);
#else
    int64_t sample_x = ((int64_t)left << GAME_3D_FIXED_SHIFT) + GAME_3D_FIXED_HALF;
    int64_t sample_y = ((int64_t)pixel_y << GAME_3D_FIXED_SHIFT) + GAME_3D_FIXED_HALF;
    int64_t edge0 = game_3d_fixed_edge(second_x_fixed, second_y_fixed,
      third_x_fixed, third_y_fixed, sample_x, sample_y);
    int64_t edge1 = game_3d_fixed_edge(third_x_fixed, third_y_fixed,
      first_x_fixed, first_y_fixed, sample_x, sample_y);
    int64_t edge2 = game_3d_fixed_edge(first_x_fixed, first_y_fixed,
      second_x_fixed, second_y_fixed, sample_x, sample_y);
#endif
    int32_t inverse_z = inverse_z_row;
    int begin_x = left, end_x = right;
#if GAME_3D_TRIANGLE_MODE == 2
    /* Keep small triangles on the addition-only path: a 64-bit divide per
     * boundary can cost more than scanning a few pixels on Cortex-M33. */
    if (triangle_fast_safe && right - left >= 15) {
      int begin = 0, end = right - left;
      if (!game_3d_triangle_span(edge0, edge0_step_x, &begin, &end) ||
          !game_3d_triangle_span(edge1, edge1_step_x, &begin, &end) ||
          !game_3d_triangle_span(edge2, edge2_step_x, &begin, &end)) {
        end_x = left - 1;
      } else {
        begin_x += begin; end_x = left + end;
        edge0 += edge0_step_x * begin;
        edge1 += edge1_step_x * begin;
        edge2 += edge2_step_x * begin;
        inverse_z = (int32_t)((int64_t)inverse_z + (int64_t)inverse_z_step_x_fixed * begin);
      }
    }
#endif
#if GAME_3D_TRIANGLE_MODE == 3
    if (triangle_fast_safe && right - left >= 15) {
      for (int pixel_x = left; pixel_x <= right; ) {
        int count = right - pixel_x + 1;
        if (count > 8) count = 8;
        int64_t last0 = edge0 + edge0_step_x * (count - 1);
        int64_t last1 = edge1 + edge1_step_x * (count - 1);
        int64_t last2 = edge2 + edge2_step_x * (count - 1);
        /* An edge is linear along a row. Endpoint rejection is exact;
         * full inclusion also avoids three edge updates per pixel. */
        bool outside = (edge0 < 0 && last0 < 0) ||
                       (edge1 < 0 && last1 < 0) || (edge2 < 0 && last2 < 0);
        bool inside = edge0 >= 0 && last0 >= 0 && edge1 >= 0 && last1 >= 0 &&
                      edge2 >= 0 && last2 >= 0;
        if (outside) {
          edge0 += edge0_step_x * count;
          edge1 += edge1_step_x * count;
          edge2 += edge2_step_x * count;
          inverse_z = (int32_t)((int64_t)inverse_z + (int64_t)inverse_z_step_x_fixed * count);
          pixel_x += count;
        } else if (inside) {
          for (int n = 0; n < count; n++, pixel_x++) {
            if (inverse_z > 0) {
              size_t offset = (size_t)pixel_y * game_3d_internal_width +
                              (size_t)pixel_x;
              uint16_t depth = game_3d_depth_value_fixed(inverse_z);
              if (depth > game_3d_depth_buffer[offset]) {
                game_3d_depth_buffer[offset] = depth;
                game_3d_write_pixel(pixel_x, pixel_y, high, low);
              }
            }
            inverse_z += inverse_z_step_x_fixed;
          }
          edge0 += edge0_step_x * count;
          edge1 += edge1_step_x * count;
          edge2 += edge2_step_x * count;
        } else {
          for (int n = 0; n < count; n++, pixel_x++) {
            if (edge0 >= 0 && edge1 >= 0 && edge2 >= 0 && inverse_z > 0) {
              size_t offset = (size_t)pixel_y * game_3d_internal_width +
                              (size_t)pixel_x;
              uint16_t depth = game_3d_depth_value_fixed(inverse_z);
              if (depth > game_3d_depth_buffer[offset]) {
                game_3d_depth_buffer[offset] = depth;
                game_3d_write_pixel(pixel_x, pixel_y, high, low);
              }
            }
            edge0 += edge0_step_x;
            edge1 += edge1_step_x;
            edge2 += edge2_step_x;
            inverse_z += inverse_z_step_x_fixed;
          }
        }
      }
    } else
#endif
    {
      for (int pixel_x = begin_x; pixel_x <= end_x; pixel_x++) {
        if (edge0 >= 0 && edge1 >= 0 && edge2 >= 0 && inverse_z > 0) {
          size_t offset = (size_t)pixel_y * game_3d_internal_width +
                          (size_t)pixel_x;
          uint16_t depth = game_3d_depth_value_fixed(inverse_z);
          if (depth > game_3d_depth_buffer[offset]) {
            game_3d_depth_buffer[offset] = depth;
            game_3d_write_pixel(pixel_x, pixel_y, high, low);
          }
        }
        edge0 += edge0_step_x;
        edge1 += edge1_step_x;
        edge2 += edge2_step_x;
        inverse_z += inverse_z_step_x_fixed;
      }
    }
#if GAME_3D_TRIANGLE_MODE != 0
    if (triangle_fast_safe) {
      edge0_row += edge0_step_y;
      edge1_row += edge1_step_y;
      edge2_row += edge2_step_y;
    }
#endif
    inverse_z_row += inverse_z_step_y_fixed;
  }
}

static int64_t
game_3d_fixed_pixel_coordinate(int64_t coordinate)
{
  if (coordinate >= 0) {
    return (coordinate + GAME_3D_FIXED_HALF) >> GAME_3D_FIXED_SHIFT;
  }
  return -((-coordinate + GAME_3D_FIXED_HALF) >> GAME_3D_FIXED_SHIFT);
}

/* Exact minor-axis advance and remainder for Bresenham's strict tie rule.
 * Use a multiply/divide normally, bounded long multiplication for huge near-
 * plane projections. Neither step*minor nor its error reconstruction overflows. */
static uint64_t
game_3d_line_minor_at(uint64_t step, uint64_t major, uint64_t minor,
                      uint64_t *remainder)
{
  uint64_t bias = (major - 1u) / 2u;
  uint64_t quotient = 0, rest = 0;
  if (minor == 0 || step <= (UINT64_MAX - bias) / minor) {
    uint64_t value = step * minor + bias;
    *remainder = value % major;
    return value / major;
  }
  uint64_t bit = UINT64_C(1) << 63;
  while ((step & bit) == 0) bit >>= 1;
  for (; bit != 0; bit >>= 1) {
    quotient *= 2u;
    rest *= 2u;
    if (rest >= major) { rest -= major; quotient++; }
    if (step & bit) {
      rest += minor;
      if (rest >= major) { rest -= major; quotient++; }
    }
  }
  rest += bias;
  if (rest >= major) { rest -= major; quotient++; }
  *remainder = rest;
  return quotient;
}

static void
game_3d_raster_line(const struct game_3d_projected_vertex *first,
                    const struct game_3d_projected_vertex *second,
                    uint16_t color, bool depth_test)
{
  int64_t x0 = game_3d_fixed_pixel_coordinate(first->x_fixed);
  int64_t y0 = game_3d_fixed_pixel_coordinate(first->y_fixed);
  int64_t x1 = game_3d_fixed_pixel_coordinate(second->x_fixed);
  int64_t y1 = game_3d_fixed_pixel_coordinate(second->y_fixed);
  int64_t delta_x = x1 - x0;
  int64_t delta_y = y1 - y0;
  int64_t absolute_x = delta_x < 0 ? -delta_x : delta_x;
  int64_t absolute_y = delta_y < 0 ? -delta_y : delta_y;
  int64_t steps = absolute_x > absolute_y ? absolute_x : absolute_y;
  int step_x = delta_x < 0 ? -1 : 1;
  int step_y = delta_y < 0 ? -1 : 1;
  int64_t error = absolute_x - absolute_y;
  int64_t min_x = x0 < x1 ? x0 : x1;
  int64_t max_x = x0 > x1 ? x0 : x1;
  int64_t min_y = y0 < y1 ? y0 : y1;
  int64_t max_y = y0 > y1 ? y0 : y1;
  int64_t inverse_z = first->inverse_z_fixed;
  int64_t inverse_z_step = steps == 0
    ? 0
    : ((int64_t)second->inverse_z_fixed - first->inverse_z_fixed) / steps;
  uint8_t high = (uint8_t)(color >> 8);
  uint8_t low = (uint8_t)color;

  if (max_x < 0 || max_y < 0 ||
      min_x >= (int)game_3d_internal_width ||
      min_y >= (int)game_3d_internal_height) {
    return;
  }
  if (min_x < 0) min_x = 0;
  if (min_y < 0) min_y = 0;
  if (max_x >= (int)game_3d_internal_width) {
    max_x = (int)game_3d_internal_width - 1;
  }
  if (max_y >= (int)game_3d_internal_height) {
    max_y = (int)game_3d_internal_height - 1;
  }
  game_3d_add_dirty_rect(min_x, min_y, max_x, max_y);

  int64_t begin = 0, end = steps;
#if GAME_3D_LINE_CLIP
  if (steps > 0) {
    bool x_major = absolute_x >= absolute_y;
    int64_t origin = x_major ? x0 : y0;
    int direction = x_major ? step_x : step_y;
    int64_t limit = (x_major ? game_3d_internal_width : game_3d_internal_height) - 1;
    begin = direction > 0 ? -origin : origin - limit;
    end = direction > 0 ? limit - origin : origin;
    if (begin < 0) begin = 0;
    if (end > steps) end = steps;
    if (begin > end) return;
    if (begin > 0) {
      uint64_t rest;
      int64_t minor = x_major ? absolute_y : absolute_x;
      int64_t moved = (int64_t)game_3d_line_minor_at(begin, steps, minor, &rest);
      int64_t bias = (steps - 1) / 2;
      x0 += step_x * (x_major ? begin : moved);
      y0 += step_y * (x_major ? moved : begin);
      error += x_major ? bias - (int64_t)rest : (int64_t)rest - bias;
      inverse_z += begin * inverse_z_step;
    }
  }
#endif
  for (int64_t sample = begin; ; sample++) {
    if (x0 >= 0 && y0 >= 0 &&
        x0 < (int)game_3d_internal_width &&
        y0 < (int)game_3d_internal_height &&
        (!depth_test || inverse_z > 0)) {
      size_t offset = (size_t)y0 * game_3d_internal_width + (size_t)x0;
      if (!depth_test) {
        game_3d_write_pixel(x0, y0, high, low);
      } else {
        uint16_t depth = game_3d_depth_value_fixed(inverse_z);
        if (depth > game_3d_depth_buffer[offset]) {
          game_3d_depth_buffer[offset] = depth;
          game_3d_write_pixel(x0, y0, high, low);
        }
      }
    }
    if (sample == end) break;

    {
      int64_t doubled_error = error * 2;
      if (doubled_error > -absolute_y) {
        error -= absolute_y;
        x0 += step_x;
      }
      if (doubled_error < absolute_x) {
        error += absolute_x;
        y0 += step_y;
      }
    }
    inverse_z += inverse_z_step;
  }
}

static bool
game_3d_triangle_needs_clipping(const struct game_3d_vertex *input)
{
  for (int index = 0; index < 3; index++) {
    if (input[index].z < game_3d_camera.near_clip ||
        input[index].z > game_3d_camera.far_clip) {
      return true;
    }
  }
  return false;
}

static bool
game_3d_clip_segment_z(const struct game_3d_vertex *first,
                       const struct game_3d_vertex *second,
                       struct game_3d_vertex *clipped_first,
                       struct game_3d_vertex *clipped_second)
{
  float delta_z = second->z - first->z;
  float start = 0.0f;
  float end = 1.0f;

  if (delta_z == 0.0f) {
    if (first->z < game_3d_camera.near_clip ||
        first->z > game_3d_camera.far_clip) {
      return false;
    }
  } else if (delta_z > 0.0f) {
    float near_start = (game_3d_camera.near_clip - first->z) / delta_z;
    float far_end = (game_3d_camera.far_clip - first->z) / delta_z;
    if (near_start > start) start = near_start;
    if (far_end < end) end = far_end;
  } else {
    float far_start = (game_3d_camera.far_clip - first->z) / delta_z;
    float near_end = (game_3d_camera.near_clip - first->z) / delta_z;
    if (far_start > start) start = far_start;
    if (near_end < end) end = near_end;
  }
  if (start > end) return false;

  clipped_first->x = first->x + (second->x - first->x) * start;
  clipped_first->y = first->y + (second->y - first->y) * start;
  clipped_first->z = first->z + delta_z * start;
  clipped_second->x = first->x + (second->x - first->x) * end;
  clipped_second->y = first->y + (second->y - first->y) * end;
  clipped_second->z = first->z + delta_z * end;
  return true;
}

static bool
game_3d_render_float_line(const struct game_3d_vertex *first,
                          const struct game_3d_vertex *second,
                          uint16_t color, bool depth_test)
{
  struct game_3d_vertex clipped_first;
  struct game_3d_vertex clipped_second;
  struct game_3d_projected_vertex projected_first;
  struct game_3d_projected_vertex projected_second;

  if (!isfinite(first->x) || !isfinite(first->y) ||
      !isfinite(first->z) || !isfinite(second->x) ||
      !isfinite(second->y) || !isfinite(second->z)) {
    return false;
  }
  if (!game_3d_clip_segment_z(first, second, &clipped_first,
                              &clipped_second)) {
    return true;
  }
  projected_first = game_3d_project_vertex(&clipped_first);
  projected_second = game_3d_project_vertex(&clipped_second);
  if (!isfinite(projected_first.x) || !isfinite(projected_first.y) ||
      !isfinite(projected_first.inverse_z) ||
      !isfinite(projected_second.x) || !isfinite(projected_second.y) ||
      !isfinite(projected_second.inverse_z)) {
    return true;
  }
  game_3d_raster_line(&projected_first, &projected_second, color,
                      depth_test);
  return true;
}

static bool
game_3d_render_float_triangle(const struct game_3d_vertex *input,
                              uint16_t color)
{
  struct game_3d_vertex clipped_near[8];
  struct game_3d_vertex clipped_far[8];
  const struct game_3d_vertex *polygon = input;
  int polygon_count = 3;

  if (input[0].z > 0.0f && input[1].z > 0.0f && input[2].z > 0.0f) {
    float projected_area = game_3d_projected_area_numerator(&input[0],
                                                              &input[1],
                                                              &input[2]);
    if (isfinite(projected_area) && projected_area <= 0.0f) return true;
  }
  if (game_3d_triangle_needs_clipping(input)) {
    polygon_count = game_3d_clip_z(input, 3, game_3d_camera.near_clip, true,
                                   clipped_near);
    if (polygon_count < 3) return true;
    polygon_count = game_3d_clip_z(clipped_near, polygon_count,
                                   game_3d_camera.far_clip, false,
                                   clipped_far);
    if (polygon_count < 3) return true;
    polygon = clipped_far;
  }

  {
    struct game_3d_projected_vertex first =
      game_3d_project_vertex(&polygon[0]);
    if (!isfinite(first.x) || !isfinite(first.y) ||
        !isfinite(first.inverse_z)) return true;
    for (int index = 1; index + 1 < polygon_count; index++) {
      struct game_3d_projected_vertex second =
        game_3d_project_vertex(&polygon[index]);
      struct game_3d_projected_vertex third =
        game_3d_project_vertex(&polygon[index + 1]);
      if (isfinite(second.x) && isfinite(second.y) &&
          isfinite(second.inverse_z) && isfinite(third.x) &&
          isfinite(third.y) && isfinite(third.inverse_z)) {
        game_3d_raster_triangle(&first, &second, &third, color);
      }
    }
  }
  return true;
}

static bool
game_3d_fixed_vertex_inside_z(const struct game_3d_fixed_vertex *vertex)
{
  float z = (float)vertex->z / (float)GAME_3D_FIXED_ONE;
  return z >= game_3d_camera.near_clip && z <= game_3d_camera.far_clip;
}

static bool
game_3d_render_fixed_line(const struct game_3d_fixed_vertex *first,
                          const struct game_3d_fixed_vertex *second,
                          uint16_t color, bool depth_test)
{
  if (game_3d_fixed_vertex_inside_z(first) &&
      game_3d_fixed_vertex_inside_z(second)) {
    struct game_3d_projected_vertex projected_first =
      game_3d_project_shared_vertex(first);
    struct game_3d_projected_vertex projected_second =
      game_3d_project_shared_vertex(second);
    game_3d_raster_line(&projected_first, &projected_second, color,
                        depth_test);
    return true;
  }

  {
    struct game_3d_vertex float_first = game_3d_vertex_from_fixed(first);
    struct game_3d_vertex float_second = game_3d_vertex_from_fixed(second);
    return game_3d_render_float_line(&float_first, &float_second, color,
                                     depth_test);
  }
}

static bool
game_3d_render_fixed_triangle_refs(const struct game_3d_fixed_vertex *a,
                                   const struct game_3d_fixed_vertex *b,
                                   const struct game_3d_fixed_vertex *c,
                                   uint16_t color)
{
  const struct game_3d_fixed_vertex *input[3] = {a, b, c};
  struct game_3d_vertex float_input[3];
  bool inside = game_3d_fixed_vertex_inside_z(input[0]) &&
                game_3d_fixed_vertex_inside_z(input[1]) &&
                game_3d_fixed_vertex_inside_z(input[2]);

  for (int index = 0; index < 3; index++) {
    float_input[index] = game_3d_vertex_from_fixed(input[index]);
  }
  if (!inside) return game_3d_render_float_triangle(float_input, color);
  if (float_input[0].z > 0.0f && float_input[1].z > 0.0f &&
      float_input[2].z > 0.0f) {
    float projected_area = game_3d_projected_area_numerator(&float_input[0],
                                                              &float_input[1],
                                                              &float_input[2]);
    if (isfinite(projected_area) && projected_area <= 0.0f) return true;
  }

  {
    struct game_3d_projected_vertex first =
      game_3d_project_shared_vertex(input[0]);
    struct game_3d_projected_vertex second =
      game_3d_project_shared_vertex(input[1]);
    struct game_3d_projected_vertex third =
      game_3d_project_shared_vertex(input[2]);
    if (isfinite(first.x) && isfinite(first.y) &&
        isfinite(first.inverse_z) && isfinite(second.x) &&
        isfinite(second.y) && isfinite(second.inverse_z) &&
        isfinite(third.x) && isfinite(third.y) &&
        isfinite(third.inverse_z)) {
      game_3d_raster_triangle(&first, &second, &third, color);
    }
  }
  return true;
}

static bool
game_3d_render_fixed_triangle(const struct game_3d_fixed_vertex *input,
                              uint16_t color)
{
  return game_3d_render_fixed_triangle_refs(&input[0], &input[1], &input[2], color);
}

static bool
game_3d_fixed_triangle_inputs_supported(const float *values)
{
  if (!game_3d_fixed_value_supported(game_3d_camera.x) ||
      !game_3d_fixed_value_supported(game_3d_camera.y) ||
      !game_3d_fixed_value_supported(game_3d_camera.z)) return false;
  for (int index = 0; index < 9; index++) {
    if (!game_3d_fixed_value_supported(values[index])) return false;
  }
  return true;
}

bool
game_3d_draw_triangle(float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      float x3, float y3, float z3,
                      uint16_t color)
{
  const float values[9] = {x1, y1, z1, x2, y2, z2, x3, y3, z3};

  if (!game_3d_frame_active || !game_3d_supported() ||
      !isfinite(x1) || !isfinite(y1) || !isfinite(z1) ||
      !isfinite(x2) || !isfinite(y2) || !isfinite(z2) ||
      !isfinite(x3) || !isfinite(y3) || !isfinite(z3)) {
    return false;
  }
  if (game_3d_fixed_triangle_inputs_supported(values)) {
    struct game_3d_fixed_vertex input[3];
    input[0] = game_3d_transform_vertex_fixed(x1, y1, z1);
    input[1] = game_3d_transform_vertex_fixed(x2, y2, z2);
    input[2] = game_3d_transform_vertex_fixed(x3, y3, z3);
    return game_3d_render_fixed_triangle(input, color);
  }

  {
    struct game_3d_vertex input[3];
    input[0] = game_3d_transform_vertex(x1, y1, z1);
    input[1] = game_3d_transform_vertex(x2, y2, z2);
    input[2] = game_3d_transform_vertex(x3, y3, z3);
    for (int index = 0; index < 3; index++) {
      if (!isfinite(input[index].x) || !isfinite(input[index].y) ||
          !isfinite(input[index].z)) return false;
    }
    return game_3d_render_float_triangle(input, color);
  }
}

static bool
game_3d_draw_indexed_mesh_internal(const float *vertices, size_t vertex_count,
                          const uint16_t *indices, size_t triangle_count,
                          const uint16_t *colors, bool *fixed_ready)
{
  bool fixed_supported =
    game_3d_fixed_value_supported(game_3d_camera.x) &&
    game_3d_fixed_value_supported(game_3d_camera.y) &&
    game_3d_fixed_value_supported(game_3d_camera.z);

  if (fixed_ready != NULL) *fixed_ready = false;
  if (!game_3d_frame_active || !game_3d_supported() ||
      vertex_count > GAME_3D_INDEXED_MAX_VERTICES ||
      (triangle_count > 0 &&
       (vertices == NULL || indices == NULL || colors == NULL))) {
    return false;
  }
  if (triangle_count == 0) return true;
  if ((game_3d_indexed_vertices == NULL && fixed_supported) ||
      vertex_count == 0) return false;

  game_3d_projection_begin();
  for (size_t vertex = 0; vertex < vertex_count; vertex++) {
    const float *source = &vertices[vertex * 3u];
    if (!isfinite(source[0]) || !isfinite(source[1]) ||
        !isfinite(source[2])) return false;
    if (!game_3d_fixed_value_supported(source[0]) ||
        !game_3d_fixed_value_supported(source[1]) ||
        !game_3d_fixed_value_supported(source[2])) {
      fixed_supported = false;
    }
    if (fixed_supported) {
      game_3d_indexed_vertices[vertex] =
        game_3d_transform_vertex_fixed(source[0], source[1], source[2]);
    }
  }
  for (size_t triangle = 0; triangle < triangle_count; triangle++) {
    uint16_t first_index = indices[triangle * 3u];
    uint16_t second_index = indices[triangle * 3u + 1u];
    uint16_t third_index = indices[triangle * 3u + 2u];
    if (first_index >= vertex_count || second_index >= vertex_count ||
        third_index >= vertex_count) return false;
    if (!fixed_supported) {
      const float *first = &vertices[(size_t)first_index * 3u];
      const float *second = &vertices[(size_t)second_index * 3u];
      const float *third = &vertices[(size_t)third_index * 3u];
      if (!game_3d_draw_triangle(first[0], first[1], first[2],
                                 second[0], second[1], second[2],
                                 third[0], third[1], third[2],
                                 colors[triangle])) return false;
    } else {
      if (!game_3d_render_fixed_triangle_refs(
            &game_3d_indexed_vertices[first_index],
            &game_3d_indexed_vertices[second_index],
            &game_3d_indexed_vertices[third_index], colors[triangle])) {
        return false;
      }
    }
  }
  if (fixed_ready != NULL) *fixed_ready = fixed_supported;
  return true;
}

bool
game_3d_draw_indexed_mesh(const float *vertices, size_t vertex_count,
                          const uint16_t *indices, size_t triangle_count,
                          const uint16_t *colors)
{
  return game_3d_draw_indexed_mesh_internal(vertices, vertex_count, indices,
                                           triangle_count, colors, NULL);
}

bool
game_3d_draw_wireframe(const float *vertices, size_t vertex_count,
                       const uint16_t *edges, size_t edge_count,
                       uint16_t color, bool depth_test)
{
  bool fixed_supported =
    game_3d_fixed_value_supported(game_3d_camera.x) &&
    game_3d_fixed_value_supported(game_3d_camera.y) &&
    game_3d_fixed_value_supported(game_3d_camera.z);

  if (!game_3d_frame_active || !game_3d_supported() ||
      vertex_count > GAME_3D_INDEXED_MAX_VERTICES ||
      (edge_count > 0 && (vertices == NULL || edges == NULL))) {
    return false;
  }
  if (edge_count == 0) return true;
  if ((game_3d_indexed_vertices == NULL && fixed_supported) ||
      vertex_count == 0) {
    return false;
  }

  game_3d_projection_begin();
  for (size_t vertex = 0; vertex < vertex_count; vertex++) {
    const float *source = &vertices[vertex * 3u];
    if (!isfinite(source[0]) || !isfinite(source[1]) ||
        !isfinite(source[2])) {
      return false;
    }
    if (!game_3d_fixed_value_supported(source[0]) ||
        !game_3d_fixed_value_supported(source[1]) ||
        !game_3d_fixed_value_supported(source[2])) {
      fixed_supported = false;
    }
  }
  /* Select one numeric path for the entire batch before writing either
   * workspace. A later large transformed vertex must not leave the prefix
   * of the float fallback uninitialized. */
  for (size_t vertex = 0; vertex < vertex_count; vertex++) {
    const float *source = &vertices[vertex * 3u];
    if (fixed_supported) {
      game_3d_indexed_vertices[vertex] =
        game_3d_transform_vertex_fixed(source[0], source[1], source[2]);
    } else {
      game_3d_wireframe_float_vertices[vertex] =
        game_3d_transform_vertex(source[0], source[1], source[2]);
      if (!isfinite(game_3d_wireframe_float_vertices[vertex].x) ||
          !isfinite(game_3d_wireframe_float_vertices[vertex].y) ||
          !isfinite(game_3d_wireframe_float_vertices[vertex].z)) {
        return false;
      }
    }
  }

  for (size_t edge = 0; edge < edge_count; edge++) {
    uint16_t first_index = edges[edge * 2u];
    uint16_t second_index = edges[edge * 2u + 1u];
    if (first_index >= vertex_count || second_index >= vertex_count) {
      return false;
    }
    if (fixed_supported) {
      game_3d_render_fixed_line(&game_3d_indexed_vertices[first_index],
                                &game_3d_indexed_vertices[second_index],
                                color, depth_test);
    } else {
      game_3d_render_float_line(
        &game_3d_wireframe_float_vertices[first_index],
        &game_3d_wireframe_float_vertices[second_index], color, depth_test);
    }
  }
  return true;
}

/* Only a Model instance may reuse this prepared workspace across its surface
 * and edges. Public array calls still start independent projection epochs. */
bool
game_3d_draw_model_geometry(const float *vertices, size_t vertex_count,
                            const uint16_t *indices, size_t triangle_count,
                            const uint16_t *colors, const uint16_t *edges,
                            size_t edge_count, uint16_t color, bool depth_test)
{
#if GAME_3D_MODEL_SHARED
  if (triangle_count > 0 && edge_count > 0) {
    bool fixed_ready;
    if (!game_3d_draw_indexed_mesh_internal(vertices, vertex_count, indices,
                                            triangle_count, colors, &fixed_ready)) {
      return false;
    }
    if (fixed_ready) {
      if (edges == NULL) return false;
      for (size_t edge = 0; edge < edge_count; edge++) {
        uint16_t first = edges[edge * 2u], second = edges[edge * 2u + 1u];
        if (first >= vertex_count || second >= vertex_count) return false;
        game_3d_render_fixed_line(&game_3d_indexed_vertices[first],
                                   &game_3d_indexed_vertices[second], color, depth_test);
      }
      return true;
    }
    /* Keep the existing per-triangle float fallback and independent wire
     * path, including mixed fixed-point ranges. No numeric rules change. */
    return game_3d_draw_wireframe(vertices, vertex_count, edges, edge_count,
                                   color, depth_test);
  }
#endif
  return game_3d_draw_indexed_mesh(vertices, vertex_count, indices,
                                   triangle_count, colors) &&
    (edge_count == 0 || game_3d_draw_wireframe(vertices, vertex_count, edges,
                                              edge_count, color, depth_test));
}

bool
game_display_fill_rect(int x, int y, int width, int height, uint16_t color,
                       uint8_t alpha)
{
  int64_t right;
  int64_t bottom;
  int clipped_x;
  int clipped_y;
  int clipped_right;
  int clipped_bottom;
  int clipped_width;

  if (width <= 0 || height <= 0) {
    return false;
  }
  if (alpha == 0) {
    return true;
  }

#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif

  /* Games commonly let a sprite or UI bar cross an edge. Keep that a
   * harmless no-op/partial draw instead of turning it into a Ruby exception.
   * Use 64-bit endpoints so a bad coordinate cannot overflow before clipping. */
  right = (int64_t)x + (int64_t)width;
  bottom = (int64_t)y + (int64_t)height;
  if (right <= 0 || bottom <= 0 || x >= (int)GAME_DISPLAY_WIDTH ||
      y >= (int)GAME_DISPLAY_HEIGHT) {
#if GAME_ENABLE_PROFILING
    profile_add(&profile_rect_us, profile_started);
#endif
    return true;
  }

  clipped_x = x < 0 ? 0 : x;
  clipped_y = y < 0 ? 0 : y;
  clipped_right = right > (int64_t)GAME_DISPLAY_WIDTH
                    ? (int)GAME_DISPLAY_WIDTH
                    : (int)right;
  clipped_bottom = bottom > (int64_t)GAME_DISPLAY_HEIGHT
                     ? (int)GAME_DISPLAY_HEIGHT
                     : (int)bottom;
  if (clipped_x >= clipped_right || clipped_y >= clipped_bottom) {
#if GAME_ENABLE_PROFILING
    profile_add(&profile_rect_us, profile_started);
#endif
    return true;
  }

  clipped_width = clipped_right - clipped_x;
  if (alpha == UINT8_MAX) {
    for (int row = clipped_y; row < clipped_bottom; row++) {
      uint8_t *dst = framebuffer +
        ((size_t)row * GAME_DISPLAY_WIDTH + (size_t)clipped_x) * 2;
      fill_rgb565_row(dst, (size_t)clipped_width, color);
    }
  } else {
    for (int row = clipped_y; row < clipped_bottom; row++) {
      for (int column = clipped_x; column < clipped_right; column++) {
        uint8_t *dst = framebuffer +
          ((size_t)row * GAME_DISPLAY_WIDTH + (size_t)column) * 2u;
        uint16_t destination = (uint16_t)(((uint16_t)dst[0] << 8) | dst[1]);
        uint16_t blended;
        uint8_t source_red = (uint8_t)((color >> 11) & 0x1f);
        uint8_t source_green = (uint8_t)((color >> 5) & 0x3f);
        uint8_t source_blue = (uint8_t)(color & 0x1f);
        uint8_t destination_red = (uint8_t)((destination >> 11) & 0x1f);
        uint8_t destination_green = (uint8_t)((destination >> 5) & 0x3f);
        uint8_t destination_blue = (uint8_t)(destination & 0x1f);
        uint16_t inverse = (uint16_t)(UINT8_MAX - alpha);

        blended = (uint16_t)((((uint16_t)source_red * alpha +
                              (uint16_t)destination_red * inverse + 127u) /
                             UINT8_MAX) << 11);
        blended |= (uint16_t)((((uint16_t)source_green * alpha +
                                (uint16_t)destination_green * inverse + 127u) /
                               UINT8_MAX) << 5);
        blended |= (uint16_t)(((uint16_t)source_blue * alpha +
                               (uint16_t)destination_blue * inverse + 127u) /
                              UINT8_MAX);
        dst[0] = (uint8_t)(blended >> 8);
        dst[1] = (uint8_t)blended;
      }
    }
  }
  display_dirty_add(clipped_x, clipped_y, clipped_width,
                    clipped_bottom - clipped_y);
#if GAME_ENABLE_PROFILING
  profile_add(&profile_rect_us, profile_started);
#endif
  return true;
}

static void
display_blend_pixel(int x, int y, uint16_t color, uint8_t alpha)
{
  uint8_t *destination;
  uint16_t destination_color;
  uint16_t inverse;
  uint16_t red;
  uint16_t green;
  uint16_t blue;
  uint16_t destination_red;
  uint16_t destination_green;
  uint16_t destination_blue;
  uint16_t blended;

  if (alpha == 0 || x < 0 || y < 0 || x >= (int)GAME_DISPLAY_WIDTH ||
      y >= (int)GAME_DISPLAY_HEIGHT) {
    return;
  }
  destination = framebuffer +
    ((size_t)y * GAME_DISPLAY_WIDTH + (size_t)x) * 2u;
  if (alpha == UINT8_MAX) {
    destination[0] = (uint8_t)(color >> 8);
    destination[1] = (uint8_t)color;
    return;
  }

  destination_color = (uint16_t)(((uint16_t)destination[0] << 8) |
                                 destination[1]);
  inverse = (uint16_t)(UINT8_MAX - alpha);
  red = (uint16_t)((color >> 11) & 0x1f);
  green = (uint16_t)((color >> 5) & 0x3f);
  blue = (uint16_t)(color & 0x1f);
  destination_red = (uint16_t)((destination_color >> 11) & 0x1f);
  destination_green = (uint16_t)((destination_color >> 5) & 0x3f);
  destination_blue = (uint16_t)(destination_color & 0x1f);
  blended = (uint16_t)((((red * alpha + destination_red * inverse + 127u) /
                         UINT8_MAX) << 11) |
                       (((green * alpha + destination_green * inverse + 127u) /
                         UINT8_MAX) << 5) |
                       ((blue * alpha + destination_blue * inverse + 127u) /
                        UINT8_MAX));
  destination[0] = (uint8_t)(blended >> 8);
  destination[1] = (uint8_t)blended;
}

static void
display_draw_filled_circle(int x, int y, int radius, uint16_t color,
                           uint8_t alpha)
{
  int64_t left = (int64_t)x - radius;
  int64_t top = (int64_t)y - radius;
  int64_t right = (int64_t)x + radius;
  int64_t bottom = (int64_t)y + radius;
  int clipped_left = left < 0 ? 0 : (int)left;
  int clipped_top = top < 0 ? 0 : (int)top;
  int clipped_right = right >= (int64_t)GAME_DISPLAY_WIDTH
                        ? (int)GAME_DISPLAY_WIDTH - 1 : (int)right;
  int clipped_bottom = bottom >= (int64_t)GAME_DISPLAY_HEIGHT
                         ? (int)GAME_DISPLAY_HEIGHT - 1 : (int)bottom;
  int64_t radius_squared = (int64_t)radius * radius;

  if (clipped_left > clipped_right || clipped_top > clipped_bottom) return;
  for (int pixel_y = clipped_top; pixel_y <= clipped_bottom; pixel_y++) {
    int64_t dy = (int64_t)pixel_y - y;
    for (int pixel_x = clipped_left; pixel_x <= clipped_right; pixel_x++) {
      int64_t dx = (int64_t)pixel_x - x;
      if (dx * dx + dy * dy <= radius_squared) {
        display_blend_pixel(pixel_x, pixel_y, color, alpha);
      }
    }
  }
}

bool
game_display_draw_circle(int x, int y, int radius, uint16_t color,
                         uint8_t alpha)
{
  if (radius < 0 || radius > (int)GAME_DISPLAY_WIDTH || alpha == 0) {
    return radius >= 0 && radius <= (int)GAME_DISPLAY_WIDTH;
  }
  display_draw_filled_circle(x, y, radius, color, alpha);
  {
    int64_t left = (int64_t)x - radius;
    int64_t top = (int64_t)y - radius;
    int64_t right = (int64_t)x + radius + 1;
    int64_t bottom = (int64_t)y + radius + 1;
    int clipped_left = left < 0 ? 0 :
      (left >= (int64_t)GAME_DISPLAY_WIDTH ? (int)GAME_DISPLAY_WIDTH : (int)left);
    int clipped_top = top < 0 ? 0 :
      (top >= (int64_t)GAME_DISPLAY_HEIGHT ? (int)GAME_DISPLAY_HEIGHT : (int)top);
    int clipped_right = right <= 0 ? 0 :
      (right > (int64_t)GAME_DISPLAY_WIDTH ? (int)GAME_DISPLAY_WIDTH : (int)right);
    int clipped_bottom = bottom <= 0 ? 0 :
      (bottom > (int64_t)GAME_DISPLAY_HEIGHT ? (int)GAME_DISPLAY_HEIGHT : (int)bottom);
    if (clipped_left < clipped_right && clipped_top < clipped_bottom) {
      display_dirty_add(clipped_left, clipped_top,
                        clipped_right - clipped_left,
                        clipped_bottom - clipped_top);
    }
  }
  return true;
}

bool
game_display_draw_line(int x1, int y1, int x2, int y2, int thickness,
                       uint16_t color, uint8_t alpha)
{
  double dx;
  double dy;
  double length_squared;
  double radius;
  int64_t left;
  int64_t top;
  int64_t right;
  int64_t bottom;
  int clipped_left;
  int clipped_top;
  int clipped_right;
  int clipped_bottom;

  if (thickness <= 0 || thickness > (int)GAME_DISPLAY_WIDTH) return false;
  if (alpha == 0) return true;
  dx = (double)x2 - (double)x1;
  dy = (double)y2 - (double)y1;
  length_squared = dx * dx + dy * dy;
  radius = (double)thickness / 2.0;
  left = (int64_t)(x1 < x2 ? x1 : x2) - thickness;
  top = (int64_t)(y1 < y2 ? y1 : y2) - thickness;
  right = (int64_t)(x1 > x2 ? x1 : x2) + thickness;
  bottom = (int64_t)(y1 > y2 ? y1 : y2) + thickness;
  clipped_left = left < 0 ? 0 : (left >= (int64_t)GAME_DISPLAY_WIDTH
                                    ? (int)GAME_DISPLAY_WIDTH : (int)left);
  clipped_top = top < 0 ? 0 : (top >= (int64_t)GAME_DISPLAY_HEIGHT
                                  ? (int)GAME_DISPLAY_HEIGHT : (int)top);
  clipped_right = right >= (int64_t)GAME_DISPLAY_WIDTH
                    ? (int)GAME_DISPLAY_WIDTH - 1
                    : (right < 0 ? -1 : (int)right);
  clipped_bottom = bottom >= (int64_t)GAME_DISPLAY_HEIGHT
                     ? (int)GAME_DISPLAY_HEIGHT - 1
                     : (bottom < 0 ? -1 : (int)bottom);
  if (clipped_left > clipped_right || clipped_top > clipped_bottom) {
    return true;
  }

  for (int pixel_y = clipped_top; pixel_y <= clipped_bottom; pixel_y++) {
    for (int pixel_x = clipped_left; pixel_x <= clipped_right; pixel_x++) {
      double t;
      double closest_x;
      double closest_y;
      double distance_x;
      double distance_y;

      if (length_squared == 0.0) {
        distance_x = (double)pixel_x - x1;
        distance_y = (double)pixel_y - y1;
      } else {
        t = (((double)pixel_x - x1) * dx +
             ((double)pixel_y - y1) * dy) / length_squared;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        closest_x = x1 + t * dx;
        closest_y = y1 + t * dy;
        distance_x = (double)pixel_x - closest_x;
        distance_y = (double)pixel_y - closest_y;
      }
      if (distance_x * distance_x + distance_y * distance_y <=
          radius * radius) {
        display_blend_pixel(pixel_x, pixel_y, color, alpha);
      }
    }
  }
  display_dirty_add(clipped_left, clipped_top,
                    clipped_right - clipped_left + 1,
                    clipped_bottom - clipped_top + 1);
  return true;
}

bool
game_display_blit_rgb565(int x, int y, int width, int height,
                         const uint8_t *data, size_t data_len)
{
  if (data == NULL || x < 0 || y < 0 || width <= 0 || height <= 0 ||
      x + width > (int)GAME_DISPLAY_WIDTH ||
      y + height > (int)GAME_DISPLAY_HEIGHT ||
      data_len != (size_t)width * (size_t)height * 2) {
    return false;
  }

#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif
#if GAME_ENABLE_BACKGROUND_LAYER
  bool full_frame = x == 0 && y == 0 &&
                    width == (int)GAME_DISPLAY_WIDTH &&
                    height == (int)GAME_DISPLAY_HEIGHT &&
                    data_len == GAME_FRAMEBUFFER_BYTES;

  if (full_frame && !background_explicit) {
    background_legacy_full_blit(data, data_len);
  } else {
#endif
  for (int row = 0; row < height; row++) {
    uint8_t *dst = framebuffer +
      ((size_t)(y + row) * GAME_DISPLAY_WIDTH + (size_t)x) * 2;
    memcpy(dst, data + (size_t)row * (size_t)width * 2, (size_t)width * 2);
  }
  display_dirty_add(x, y, width, height);
#if GAME_ENABLE_BACKGROUND_LAYER
  }
#endif
#if GAME_ENABLE_PROFILING
  profile_add(&profile_background_us, profile_started);
#endif
  return true;
}

bool
game_display_blit_rgb565_region(int x, int y, int width, int height,
                                 const uint8_t *data, size_t data_len,
                                 int source_width, int source_x, int source_y)
{
  size_t source_row_bytes;
  size_t source_height;

  if (data == NULL || x < 0 || y < 0 || width <= 0 || height <= 0 ||
      (int64_t)x + (int64_t)width > (int64_t)GAME_DISPLAY_WIDTH ||
      (int64_t)y + (int64_t)height > (int64_t)GAME_DISPLAY_HEIGHT ||
      source_width <= 0 || source_x < 0 || source_y < 0 ||
      (int64_t)source_x + (int64_t)width > (int64_t)source_width ||
      (size_t)source_width > SIZE_MAX / 2u) {
    return false;
  }
  source_row_bytes = (size_t)source_width * 2u;
  if (source_row_bytes == 0 || data_len % source_row_bytes != 0) {
    return false;
  }
  source_height = data_len / source_row_bytes;
  if ((size_t)source_y + (size_t)height > source_height) {
    return false;
  }

#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif
  for (int row = 0; row < height; row++) {
    uint8_t *destination = framebuffer +
      ((size_t)(y + row) * GAME_DISPLAY_WIDTH + (size_t)x) * 2u;
    const uint8_t *source = data +
      ((size_t)(source_y + row) * (size_t)source_width +
       (size_t)source_x) * 2u;
    memcpy(destination, source, (size_t)width * 2u);
  }
  display_dirty_add(x, y, width, height);
#if GAME_ENABLE_PROFILING
  profile_add(&profile_background_us, profile_started);
#endif
  return true;
}

bool
game_display_draw_sprite(const game_sprite_t *sprite, int x, int y,
                         uint16_t frame)
{
  int source_x = 0;
  int source_y = 0;
  int destination_x = x;
  int destination_y = y;
  int visible_width = 0;
  int visible_height = 0;
  int64_t right;
  int64_t bottom;

  if (!tft_ready || sprite == NULL || frame >= sprite->frame_count) {
    return false;
  }

#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif

  /* Clip once per sprite instead of once per pixel. This matters for
   * partially visible sprites and also gives the transparent path a tight
   * row/column loop. */
  right = (int64_t)x + (int64_t)sprite->frame_width;
  bottom = (int64_t)y + (int64_t)sprite->frame_height;
  if (right > 0 && bottom > 0 && x < (int)GAME_DISPLAY_WIDTH &&
      y < (int)GAME_DISPLAY_HEIGHT) {
    if (x < 0) {
      source_x = -x;
      destination_x = 0;
    }
    if (y < 0) {
      source_y = -y;
      destination_y = 0;
    }
    visible_width = (int)sprite->frame_width - source_x;
    visible_height = (int)sprite->frame_height - source_y;
    if (right > (int64_t)GAME_DISPLAY_WIDTH) {
      visible_width = (int)GAME_DISPLAY_WIDTH - destination_x;
    }
    if (bottom > (int64_t)GAME_DISPLAY_HEIGHT) {
      visible_height = (int)GAME_DISPLAY_HEIGHT - destination_y;
    }
  }

  for (int row = 0; row < visible_height; row++) {
    const uint8_t *source = sprite->pixels +
      (((size_t)(source_y + row) * sprite->sheet_width) +
       (size_t)frame * sprite->frame_width + (size_t)source_x) * 2u;
    uint8_t *destination = framebuffer +
      (((size_t)(destination_y + row) * GAME_DISPLAY_WIDTH) +
       (size_t)destination_x) * 2u;
    if (sprite->opaque) {
      memcpy(destination, source, (size_t)visible_width * 2u);
    } else {
      draw_sprite_transparent_row(source, destination,
                                  (size_t)visible_width, sprite->key_color);
    }
  }
  display_dirty_add(x, y, sprite->frame_width, sprite->frame_height);
#if GAME_ENABLE_PROFILING
  uint32_t profile_elapsed = profile_now() - profile_started;
  profile_sprite_calls++;
  if (sprite->cached) {
    profile_sprite_cached_calls++;
    profile_sprite_cached_us += profile_elapsed;
  } else {
    profile_sprite_psram_us += profile_elapsed;
  }
  profile_sprite_us += profile_elapsed;
#endif
  return true;
}

game_sprite_t *
game_sprite_load(const char *path, uint16_t frame_width,
                uint16_t frame_height, uint16_t frame_count,
                uint16_t key_color)
{
  size_t file_size;
  size_t expected_size;
  size_t loaded;
  game_sprite_t *sprite;

  if (path == NULL || frame_width == 0 || frame_height == 0 ||
      frame_count == 0 ||
      (size_t)frame_width * (size_t)frame_count > UINT16_MAX) {
    return NULL;
  }
  if (!game_storage_file_size(path, &file_size)) {
    return NULL;
  }
  expected_size = (size_t)frame_width * (size_t)frame_height *
                  (size_t)frame_count * 2u;
  if (file_size != expected_size) {
    printf("SPRITE: size mismatch path=%s got=%lu expected=%lu\n", path,
           (unsigned long)file_size, (unsigned long)expected_size);
    return NULL;
  }

  sprite = (game_sprite_t *)asset_alloc(sizeof(*sprite));
  if (sprite == NULL) {
    printf("SPRITE: descriptor does not fit in PSRAM media area\n");
    goto load_failed;
  }
  sprite->pixels = sprite_cache_alloc(file_size, 4u);
  sprite->cached = sprite->pixels != NULL;
  if (sprite->pixels == NULL) {
    sprite->pixels = (uint8_t *)asset_alloc(file_size);
  }
  if (sprite->pixels == NULL) {
    printf("SPRITE: asset does not fit in PSRAM media area path=%s\n", path);
    goto load_failed;
  }
  loaded = 0;
  if (!game_storage_read_file(path, sprite->pixels, file_size, &loaded) ||
      loaded != file_size) {
    printf("SPRITE: asset read failed path=%s\n", path);
    goto load_failed;
  }
  sprite->opaque = true;
  for (size_t offset = 0; offset < loaded; offset += 2u) {
    uint16_t color = (uint16_t)(((uint16_t)sprite->pixels[offset] << 8) |
                                sprite->pixels[offset + 1u]);
    if (color == key_color) {
      sprite->opaque = false;
      break;
    }
  }
  sprite->bytes = loaded;
  sprite->frame_width = frame_width;
  sprite->frame_height = frame_height;
  sprite->frame_count = frame_count;
  sprite->sheet_width = (uint16_t)(frame_width * frame_count);
  sprite->key_color = key_color;
  printf("SPRITE: loaded path=%s %ux%u frames=%u storage=%s "
         "cache=%lu/%u opaque=%s\n", path, frame_width, frame_height,
         frame_count,
         sprite->cached ? "SRAM" : "PSRAM",
         (unsigned long)sprite_cache_next,
         (unsigned)GAME_SPRITE_CACHE_BYTES,
         sprite->opaque ? "yes" : "no");
  asset_resident_bytes += file_size;
  return sprite;

load_failed:
  if (sprite != NULL) {
    if (sprite->cached) sprite_cache_free(sprite->pixels);
    else asset_free(sprite->pixels);
    asset_free(sprite);
  }
  return NULL;
}

uint16_t

game_sprite_frame_width(const game_sprite_t *sprite)
{
  return sprite != NULL ? sprite->frame_width : 0;
}

uint16_t
game_sprite_frame_height(const game_sprite_t *sprite)
{
  return sprite != NULL ? sprite->frame_height : 0;
}

uint16_t
game_sprite_frame_count(const game_sprite_t *sprite)
{
  return sprite != NULL ? sprite->frame_count : 0;
}

game_texture_t *
game_texture_load(const char *path, uint16_t width, uint16_t height,
                 uint16_t key_color)
{
  size_t file_size;
  size_t expected_size;
  size_t loaded;
  game_texture_t *texture;

  if (path == NULL || width == 0 || height == 0 ||
      (size_t)width > SIZE_MAX / (size_t)height ||
      (size_t)width * (size_t)height > SIZE_MAX / 2u) {
    return NULL;
  }
  if (!game_storage_file_size(path, &file_size)) {
    return NULL;
  }
  expected_size = (size_t)width * (size_t)height * 2u;
  if (file_size != expected_size) {
    printf("TEXTURE: size mismatch path=%s got=%lu expected=%lu\n", path,
           (unsigned long)file_size, (unsigned long)expected_size);
    return NULL;
  }

  texture = (game_texture_t *)asset_alloc(sizeof(*texture));
  if (texture == NULL) {
    printf("TEXTURE: descriptor does not fit in PSRAM media area\n");
    goto load_failed;
  }
  texture->pixels = (uint8_t *)asset_alloc(file_size);
  if (texture->pixels == NULL) {
    printf("TEXTURE: asset does not fit in PSRAM media area path=%s\n", path);
    goto load_failed;
  }
  loaded = 0;
  if (!game_storage_read_file(path, texture->pixels, file_size, &loaded) ||
      loaded != file_size) {
    printf("TEXTURE: asset read failed path=%s\n", path);
    goto load_failed;
  }
  texture->opaque = true;
  for (size_t offset = 0; offset < loaded; offset += 2u) {
    uint16_t color = (uint16_t)(((uint16_t)texture->pixels[offset] << 8) |
                                texture->pixels[offset + 1u]);
    if (color == key_color) {
      texture->opaque = false;
      break;
    }
  }
  texture->bytes = loaded;
  texture->width = width;
  texture->height = height;
  texture->key_color = key_color;
  printf("TEXTURE: loaded path=%s %ux%u opaque=%s\n", path, width, height,
         texture->opaque ? "yes" : "no");
  asset_resident_bytes += file_size;
  return texture;

load_failed:
  if (texture != NULL) { asset_free(texture->pixels); asset_free(texture); }
  return NULL;
}

uint16_t

game_texture_width(const game_texture_t *texture)
{
  return texture != NULL ? texture->width : 0;
}

uint16_t
game_texture_height(const game_texture_t *texture)
{
  return texture != NULL ? texture->height : 0;
}

uint32_t
game_texture_checksum(const game_texture_t *texture)
{
  if (texture == NULL) return 0;
  uint32_t checksum = UINT32_C(2166136261);
  for (size_t index = 0; index < texture->bytes; index++) {
    checksum = (checksum ^ texture->pixels[index]) * UINT32_C(16777619);
  }
  return checksum;
}

#define GAME_FONT_HEADER_BYTES 16u
#define GAME_FONT_SIZE_HEADER_BYTES 20u
#define GAME_FONT_GLYPH_RECORD_BYTES 20u

static uint16_t
font_be16(const uint8_t *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int16_t
font_be16_signed(const uint8_t *data)
{
  return (int16_t)font_be16(data);
}

static uint32_t
font_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static bool
font_range_valid(size_t offset, size_t length, size_t total)
{
  return offset <= total && length <= total - offset;
}

static bool
font_validate(const uint8_t *data, size_t bytes)
{
  uint16_t first_char;
  uint16_t glyph_count;
  uint16_t size_count;
  size_t size_table_end;

  if (data == NULL || bytes < GAME_FONT_HEADER_BYTES ||
      memcmp(data, "PBF1", 4) != 0 || font_be32(data + 12) != bytes) {
    return false;
  }
  first_char = font_be16(data + 4);
  glyph_count = font_be16(data + 6);
  size_count = font_be16(data + 8);
  if (glyph_count == 0 || size_count == 0 ||
      (uint32_t)first_char + glyph_count > 0x10000u) {
    return false;
  }
  size_table_end = GAME_FONT_HEADER_BYTES +
    (size_t)size_count * GAME_FONT_SIZE_HEADER_BYTES;
  if (size_table_end > bytes) return false;

  for (uint16_t size_index = 0; size_index < size_count; size_index++) {
    const uint8_t *size_header = data + GAME_FONT_HEADER_BYTES +
      (size_t)size_index * GAME_FONT_SIZE_HEADER_BYTES;
    uint16_t pixel_size = font_be16(size_header);
    uint16_t line_height = font_be16(size_header + 2);
    uint16_t record_count = font_be16(size_header + 8);
    uint32_t records_offset = font_be32(size_header + 12);
    uint32_t bitmap_offset = font_be32(size_header + 16);

    if (pixel_size == 0 || line_height == 0 || record_count != glyph_count ||
        !font_range_valid(records_offset,
                          (size_t)record_count * GAME_FONT_GLYPH_RECORD_BYTES,
                          bytes) || bitmap_offset > bytes) {
      return false;
    }
    for (uint16_t glyph_index = 0; glyph_index < record_count; glyph_index++) {
      const uint8_t *record = data + records_offset +
        (size_t)glyph_index * GAME_FONT_GLYPH_RECORD_BYTES;
      uint16_t width = font_be16(record);
      uint16_t height = font_be16(record + 2);
      uint32_t glyph_offset = font_be32(record + 12);
      uint32_t glyph_bytes = font_be32(record + 16);

      if ((size_t)width > SIZE_MAX / (size_t)(height == 0 ? 1 : height) ||
          glyph_bytes != (size_t)width * (size_t)height ||
          !font_range_valid(glyph_offset, glyph_bytes, bytes) ||
          glyph_offset < bitmap_offset) {
        return false;
      }
    }
  }
  return true;
}

static const uint8_t *
font_size_header(const game_font_t *font, uint16_t pixel_size)
{
  /* Only game_font_load publishes this opaque handle. Its immutable PBF size
   * table and glyph records have already passed the complete bounds check. */
  if (font == NULL) return NULL;
  for (uint16_t size_index = 0; size_index < font->size_count; size_index++) {
    const uint8_t *size_header = font->data + GAME_FONT_HEADER_BYTES +
      (size_t)size_index * GAME_FONT_SIZE_HEADER_BYTES;
    if (font_be16(size_header) == pixel_size) return size_header;
  }
  return NULL;
}

static const uint8_t *
font_glyph_record(const game_font_t *font, const uint8_t *size_header,
                 uint8_t character)
{
  uint16_t glyph_index;
  uint32_t records_offset;
  uint32_t glyph_offset;
  uint32_t glyph_bytes;
  const uint8_t *record;

  if (size_header == NULL) return NULL;
  if (character < font->first_char ||
      character >= (uint32_t)font->first_char + font->glyph_count) {
    character = '?';
  }
  if (character < font->first_char ||
      character >= (uint32_t)font->first_char + font->glyph_count) {
    return NULL;
  }
  glyph_index = (uint16_t)(character - font->first_char);
  records_offset = font_be32(size_header + 12);
  record = font->data + records_offset +
    (size_t)glyph_index * GAME_FONT_GLYPH_RECORD_BYTES;
  glyph_offset = font_be32(record + 12);
  glyph_bytes = font_be32(record + 16);
  if (!font_range_valid(glyph_offset, glyph_bytes, font->bytes)) return NULL;
  return record;
}

game_font_t *
game_font_load(const char *path)
{
  size_t file_size;
  size_t loaded;
  uint8_t *data;
  game_font_t *font;

  if (path == NULL || !game_storage_file_size(path, &file_size) ||
      file_size < GAME_FONT_HEADER_BYTES || file_size > GAME_FONT_MAX_BYTES) {
    printf("FONT: file rejected path=%s\n", path != NULL ? path : "(null)");
    return NULL;
  }
  font = (game_font_t *)asset_alloc(sizeof(*font));
  data = (uint8_t *)asset_alloc(file_size);
  if (font == NULL || data == NULL) {
    printf("FONT: media allocation failed path=%s\n", path);
    goto load_failed;
  }
  loaded = 0;
  if (!game_storage_read_file(path, data, file_size, &loaded) ||
      loaded != file_size || !font_validate(data, loaded)) {
    printf("FONT: invalid PBF path=%s bytes=%lu\n", path,
           (unsigned long)loaded);
    goto load_failed;
  }
  font->data = data;
  font->bytes = loaded;
  font->first_char = font_be16(data + 4);
  font->glyph_count = font_be16(data + 6);
  font->size_count = font_be16(data + 8);
  printf("FONT: loaded path=%s sizes=%u glyphs=%u\n", path,
         font->size_count, font->glyph_count);
  asset_resident_bytes += file_size;
  return font;

load_failed:
  asset_free(data);
  asset_free(font);
  return NULL;
}

int

game_font_measure(const game_font_t *font, const char *text,
                  uint16_t pixel_size, int spacing)
{
  const uint8_t *size_header;
  int64_t current_width = 0;
  int64_t maximum_width = 0;

  if (font == NULL || text == NULL ||
      (size_header = font_size_header(font, pixel_size)) == NULL) {
    return -1;
  }
  while (*text != '\0') {
    const uint8_t *record;
    if (*text == '\n') {
      int64_t line_width = current_width > 0 ? current_width - spacing : 0;
      if (line_width > maximum_width) maximum_width = line_width;
      current_width = 0;
      text++;
      continue;
    }
    record = font_glyph_record(font, size_header, *text++);
    if (record == NULL) return -1;
    current_width += (int64_t)font_be16_signed(record + 8) + spacing;
  }
  if (current_width > 0) current_width -= spacing;
  if (current_width > maximum_width) maximum_width = current_width;
  if (maximum_width < 0) maximum_width = 0;
  return maximum_width > INT_MAX ? INT_MAX : (int)maximum_width;
}

bool
game_display_draw_font(const game_font_t *font, int x, int y,
                       const char *text, uint16_t pixel_size, int spacing,
                       uint16_t color)
{
  const uint8_t *size_header;
  int16_t ascender;
  uint16_t line_height;
  int64_t cursor_x = x;
  int64_t baseline = (int64_t)y;

  if (font == NULL || text == NULL ||
      (size_header = font_size_header(font, pixel_size)) == NULL) {
    return false;
  }
  ascender = font_be16_signed(size_header + 4);
  line_height = font_be16(size_header + 2);
  baseline += ascender;
  while (*text != '\0') {
    const uint8_t *record;
    uint16_t width;
    uint16_t height;
    int16_t bearing_x;
    int16_t bearing_y;
    int16_t advance;
    uint32_t bitmap_offset;
    const uint8_t *bitmap;

    if (*text == '\n') {
      cursor_x = x;
      baseline += line_height;
      text++;
      continue;
    }
    record = font_glyph_record(font, size_header, *text++);
    if (record == NULL) return false;
    width = font_be16(record);
    height = font_be16(record + 2);
    bearing_x = font_be16_signed(record + 4);
    bearing_y = font_be16_signed(record + 6);
    advance = font_be16_signed(record + 8);
    bitmap_offset = font_be32(record + 12);
    bitmap = font->data + bitmap_offset;
    {
      int64_t dirty_x = cursor_x + bearing_x;
      int64_t dirty_y = baseline - bearing_y;
      int64_t dirty_right = dirty_x + width;
      int64_t dirty_bottom = dirty_y + height;
      int clipped_x = dirty_x < 0 ? 0 :
        (dirty_x >= (int64_t)GAME_DISPLAY_WIDTH ? (int)GAME_DISPLAY_WIDTH :
         (int)dirty_x);
      int clipped_y = dirty_y < 0 ? 0 :
        (dirty_y >= (int64_t)GAME_DISPLAY_HEIGHT ? (int)GAME_DISPLAY_HEIGHT :
         (int)dirty_y);
      int clipped_right = dirty_right > (int64_t)GAME_DISPLAY_WIDTH
        ? (int)GAME_DISPLAY_WIDTH : (dirty_right <= 0 ? 0 : (int)dirty_right);
      int clipped_bottom = dirty_bottom > (int64_t)GAME_DISPLAY_HEIGHT
        ? (int)GAME_DISPLAY_HEIGHT : (dirty_bottom <= 0 ? 0 : (int)dirty_bottom);
      if (clipped_x < clipped_right && clipped_y < clipped_bottom) {
        size_t source_x = (size_t)((int64_t)clipped_x - dirty_x);
        for (int destination_y = clipped_y; destination_y < clipped_bottom;
             destination_y++) {
          const uint8_t *row = bitmap +
            (size_t)((int64_t)destination_y - dirty_y) * width + source_x;
          uint8_t *destination = framebuffer +
            ((size_t)destination_y * GAME_DISPLAY_WIDTH + (size_t)clipped_x) * 2u;
          for (int destination_x = clipped_x; destination_x < clipped_right;
               destination_x++, destination += 2) {
            uint8_t alpha = *row++;
            if (alpha == UINT8_MAX) {
              destination[0] = (uint8_t)(color >> 8);
              destination[1] = (uint8_t)color;
            } else if (alpha != 0) {
              display_blend_pixel(destination_x, destination_y, color, alpha);
            }
          }
        }
        display_dirty_add(clipped_x, clipped_y, clipped_right - clipped_x,
                          clipped_bottom - clipped_y);
      }
    }
    cursor_x += (int64_t)advance + spacing;
  }
  return true;
}

static void
draw_sprite_transparent_row(const uint8_t *source, uint8_t *destination,
                            size_t pixels, uint16_t key_color)
{
  uint8_t key_hi = (uint8_t)(key_color >> 8);
  uint8_t key_lo = (uint8_t)key_color;
  uint16_t key_word = (uint16_t)key_hi | ((uint16_t)key_lo << 8);

  /* Two adjacent RGB565 pixels can be tested together. */
  if ((((uintptr_t)source & 3u) == 0u) &&
      (((uintptr_t)destination & 3u) == 0u)) {
    while (pixels >= 2) {
      uint32_t pair = *(const uint32_t *)(const void *)source;
      uint16_t first = (uint16_t)pair;
      uint16_t second = (uint16_t)(pair >> 16);

      if (first != key_word && second != key_word) {
        *(uint32_t *)(void *)destination = pair;
      } else {
        if (first != key_word) {
          destination[0] = source[0];
          destination[1] = source[1];
        }
        if (second != key_word) {
          destination[2] = source[2];
          destination[3] = source[3];
        }
      }
      source += 4;
      destination += 4;
      pixels -= 2;
    }
  }

  /* An odd destination x-coordinate can put the two streams at different
   * alignments. Keep the same two-pixel fast path there using fixed-size
   * memcpy, which is safe for both SRAM and PSRAM addresses. */
  while (pixels >= 2) {
    uint32_t pair;
    uint16_t first;
    uint16_t second;

    memcpy(&pair, source, sizeof(pair));
    first = (uint16_t)pair;
    second = (uint16_t)(pair >> 16);
    if (first != key_word && second != key_word) {
      memcpy(destination, &pair, sizeof(pair));
    } else {
      if (first != key_word) {
        destination[0] = source[0];
        destination[1] = source[1];
      }
      if (second != key_word) {
        destination[2] = source[2];
        destination[3] = source[3];
      }
    }
    source += 4;
    destination += 4;
    pixels -= 2;
  }

  while (pixels > 0) {
    if (source[0] != key_hi || source[1] != key_lo) {
      destination[0] = source[0];
      destination[1] = source[1];
    }
    source += 2;
    destination += 2;
    pixels--;
  }
}

struct game_texture_sample_axis {
  uint32_t sample;
  uint32_t fraction;
  uint32_t step;
  uint32_t remainder;
  uint32_t denominator;
};

static struct game_texture_sample_axis
texture_sample_axis(int64_t offset, int origin, int source_size,
                    int destination_size)
{
  uint64_t position = (uint64_t)offset * (uint32_t)source_size;
  uint32_t denominator = (uint32_t)destination_size;
  struct game_texture_sample_axis axis = {
    (uint32_t)origin + (uint32_t)(position / denominator),
    (uint32_t)(position % denominator),
    (uint32_t)source_size / denominator,
    (uint32_t)source_size % denominator,
    denominator
  };
  return axis;
}

static void
texture_sample_advance(struct game_texture_sample_axis *axis)
{
  axis->sample += axis->step;
  /* Each remainder is below destination_size <= INT_MAX, so their sum
   * fits uint32_t even for very large, partially off-screen destinations. */
  axis->fraction += axis->remainder;
  if (axis->fraction >= axis->denominator) {
    axis->fraction -= axis->denominator;
    axis->sample++;
  }
}

static void
draw_texture_scaled(const game_texture_t *texture, int x, int y,
                     int source_x, int source_y, int source_width,
                     int source_height, int destination_width,
                     int destination_height, int first_x, int first_y,
                     int last_x, int last_y)
{
  /* Cache only visible source columns; the source texture dimensions are
   * uint16_t. Keep this stack storage out of the existing 1:1 copy path. */
  uint16_t source_columns[GAME_DISPLAY_WIDTH];
  size_t visible_width = (size_t)(last_x - first_x);
  struct game_texture_sample_axis horizontal = texture_sample_axis(
    (int64_t)first_x - x, source_x, source_width, destination_width);
  struct game_texture_sample_axis vertical = texture_sample_axis(
    (int64_t)first_y - y, source_y, source_height, destination_height);
  uint32_t previous_row = UINT32_MAX;
  uint8_t key_high = (uint8_t)(texture->key_color >> 8);
  uint8_t key_low = (uint8_t)texture->key_color;

  for (size_t column = 0; column < visible_width; column++) {
    source_columns[column] = (uint16_t)horizontal.sample;
    texture_sample_advance(&horizontal);
  }
  for (int row = first_y; row < last_y; row++) {
    uint8_t *destination = framebuffer +
      ((size_t)row * GAME_DISPLAY_WIDTH + (size_t)first_x) * 2u;

    if (texture->opaque && vertical.sample == previous_row) {
      /* Transparent pixels must preserve this row's background, so only
       * opaque rows may reuse the previous destination row. */
      memcpy(destination, destination - GAME_DISPLAY_WIDTH * 2u,
             visible_width * 2u);
    } else {
      const uint8_t *source_row = texture->pixels +
        (size_t)vertical.sample * texture->width * 2u;
      for (size_t column = 0; column < visible_width; column++) {
        const uint8_t *source = source_row + (size_t)source_columns[column] * 2u;
        if (texture->opaque || source[0] != key_high || source[1] != key_low) {
          destination[0] = source[0];
          destination[1] = source[1];
        }
        destination += 2;
      }
    }
    previous_row = vertical.sample;
    texture_sample_advance(&vertical);
  }
}

bool
game_display_draw_texture(const game_texture_t *texture, int x, int y,
                          int source_x, int source_y, int source_width,
                          int source_height, int destination_width,
                          int destination_height)
{
  int64_t right;
  int64_t bottom;
  int64_t first_x;
  int64_t first_y;
  int64_t last_x;
  int64_t last_y;

  if (!tft_ready || texture == NULL || source_x < 0 || source_y < 0 ||
      source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
      destination_height <= 0 || source_x > texture->width ||
      source_y > texture->height ||
      source_width > (int)texture->width - source_x ||
      source_height > (int)texture->height - source_y) {
    return false;
  }

  right = (int64_t)x + (int64_t)destination_width;
  bottom = (int64_t)y + (int64_t)destination_height;
  first_x = x > 0 ? x : 0;
  first_y = y > 0 ? y : 0;
  last_x = right < (int64_t)GAME_DISPLAY_WIDTH ? right : GAME_DISPLAY_WIDTH;
  last_y = bottom < (int64_t)GAME_DISPLAY_HEIGHT ? bottom : GAME_DISPLAY_HEIGHT;

  if (last_x > first_x && last_y > first_y) {
    if (source_width == destination_width &&
        source_height == destination_height) {
      int clipped_source_x = source_x + (int)(first_x - x);
      int clipped_source_y = source_y + (int)(first_y - y);
      int visible_width = (int)(last_x - first_x);
      int visible_height = (int)(last_y - first_y);

      for (int row = 0; row < visible_height; row++) {
        const uint8_t *source = texture->pixels +
          (((size_t)(clipped_source_y + row) * texture->width) +
           (size_t)clipped_source_x) * 2u;
        uint8_t *destination = framebuffer +
          (((size_t)(first_y + row) * GAME_DISPLAY_WIDTH) +
           (size_t)first_x) * 2u;
        if (texture->opaque) {
          memcpy(destination, source, (size_t)visible_width * 2u);
        } else {
          draw_sprite_transparent_row(source, destination,
                                      (size_t)visible_width,
                                      texture->key_color);
        }
      }
    } else if (last_y - first_y == 1) {
      /* A column table cannot be reused for a single visible row. Keep the
       * direct sampler here rather than paying its setup and storage cost. */
      int source_row = source_y + (int)(((first_y - y) * source_height) /
                                        destination_height);
      uint8_t *destination = framebuffer +
        ((size_t)first_y * GAME_DISPLAY_WIDTH + (size_t)first_x) * 2u;
      for (int64_t column = first_x; column < last_x; column++) {
        int source_column = source_x + (int)(((column - x) * source_width) /
                                            destination_width);
        const uint8_t *pixel = texture->pixels +
          ((size_t)source_row * texture->width + (size_t)source_column) * 2u;
        if (texture->opaque || pixel[0] != (uint8_t)(texture->key_color >> 8) ||
            pixel[1] != (uint8_t)texture->key_color) {
          destination[0] = pixel[0];
          destination[1] = pixel[1];
        }
        destination += 2;
      }
    } else {
      draw_texture_scaled(texture, x, y, source_x, source_y, source_width,
                           source_height, destination_width, destination_height,
                           (int)first_x, (int)first_y, (int)last_x, (int)last_y);
    }
  }
  display_dirty_add(x, y, destination_width, destination_height);
  return true;
}

#if GAME_ENABLE_DIRTY_RECT
static void
tft_transfer_pixels(const uint8_t *data, size_t bytes)
{
  uint8_t command = 0x2c;

#if GAME_ENABLE_PROFILING
  profile_dirty_rects++;
  profile_dirty_bytes += (uint32_t)bytes;
#endif
  tft_select();
  gpio_put(TFT_DC_PIN, 0);
  spi_write_blocking(TFT_SPI, &command, 1);
  gpio_put(TFT_DC_PIN, 1);
  if (tft_dma_channel >= 0) {
    dma_channel_config config = dma_channel_get_default_config(tft_dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, spi_get_dreq(TFT_SPI, true));
    dma_channel_configure(tft_dma_channel, &config,
                          &spi_get_hw(TFT_SPI)->dr, data, bytes, true);
#if GAME_ENABLE_PROFILING
    profile_present_dma_submits++;
    {
      uint32_t wait_started = profile_now();
      dma_channel_wait_for_finish_blocking(tft_dma_channel);
      profile_present_dma_wait_us += profile_now() - wait_started;
    }
#else
    dma_channel_wait_for_finish_blocking(tft_dma_channel);
#endif
  } else {
    spi_write_blocking(TFT_SPI, data, bytes);
  }
#if GAME_ENABLE_PROFILING
  {
    uint32_t idle_started = profile_now();
    tft_wait_for_idle();
    profile_present_spi_idle_us += profile_now() - idle_started;
  }
#else
  tft_wait_for_idle();
#endif
  tft_deselect();
}

static void
tft_transfer_dirty_rect(const struct game_dirty_rect *rect)
{
  uint32_t width = (uint32_t)rect->x1 - (uint32_t)rect->x0 + 1u;
  uint32_t y = rect->y0;

#if GAME_FRAMEBUFFER_IN_SRAM || GAME_TFT_DMA_FROM_PSRAM
  if (rect->x0 == 0 && rect->x1 == GAME_DISPLAY_WIDTH - 1) {
#if GAME_ENABLE_PROFILING
    uint32_t window_started = profile_now();
    tft_set_window(rect->x0, rect->y0, rect->x1, rect->y1);
    profile_present_window_us += profile_now() - window_started;
#else
    tft_set_window(rect->x0, rect->y0, rect->x1, rect->y1);
#endif
    tft_transfer_pixels(framebuffer +
                        (size_t)rect->y0 * GAME_DISPLAY_WIDTH * 2u,
                        (size_t)width *
                        ((uint32_t)rect->y1 - (uint32_t)rect->y0 + 1u) * 2u);
    return;
  }
#endif

  while (y <= rect->y1) {
    uint32_t rows = (uint32_t)rect->y1 - y + 1u;
    if (rows > TFT_STRIPE_ROWS) rows = TFT_STRIPE_ROWS;
#if GAME_ENABLE_PROFILING
    uint32_t copy_started = profile_now();
#endif
    for (uint32_t row = 0; row < rows; row++) {
      const uint8_t *source = framebuffer +
        (((size_t)(y + row) * GAME_DISPLAY_WIDTH) + rect->x0) * 2u;
#if GAME_TFT_PIPELINE
      memcpy(tft_stripe[0] + (size_t)row * (size_t)width * 2u,
#else
      memcpy(tft_stripe + (size_t)row * (size_t)width * 2u,
#endif
             source, (size_t)width * 2u);
    }
#if GAME_ENABLE_PROFILING
    profile_present_copy_us += profile_now() - copy_started;
#endif
#if GAME_ENABLE_PROFILING
    uint32_t window_started = profile_now();
    tft_set_window(rect->x0, (uint16_t)y, rect->x1,
                   (uint16_t)(y + rows - 1u));
    profile_present_window_us += profile_now() - window_started;
#else
    tft_set_window(rect->x0, (uint16_t)y, rect->x1,
                   (uint16_t)(y + rows - 1u));
#endif
#if GAME_TFT_PIPELINE
    tft_transfer_pixels(tft_stripe[0], (size_t)rows * (size_t)width * 2u);
#else
    tft_transfer_pixels(tft_stripe, (size_t)rows * (size_t)width * 2u);
#endif
    y += rows;
  }
}

#if GAME_TFT_PIPELINE
struct tft_pipeline_cursor {
  size_t rect_index;
  uint32_t y;
  bool rect_active;
};

struct tft_pipeline_chunk {
  uint16_t x0;
  uint16_t y0;
  uint16_t x1;
  uint16_t y1;
  const uint8_t *data;
  size_t bytes;
};

static bool
tft_pipeline_next_chunk(struct tft_pipeline_cursor *cursor,
                         uint8_t buffer_index,
                         struct tft_pipeline_chunk *chunk)
{
  while (cursor->rect_index < dirty_rect_count) {
    const struct game_dirty_rect *rect = &dirty_rects[cursor->rect_index];
    uint32_t width = (uint32_t)rect->x1 - (uint32_t)rect->x0 + 1u;
    uint32_t rows;
    uint32_t y;

    if (!cursor->rect_active) {
      cursor->y = rect->y0;
      cursor->rect_active = true;
    }
    y = cursor->y;
    rows = (uint32_t)rect->y1 - y + 1u;
    if (rows > TFT_STRIPE_ROWS) rows = TFT_STRIPE_ROWS;

    chunk->x0 = rect->x0;
    chunk->y0 = (uint16_t)y;
    chunk->x1 = rect->x1;
    chunk->y1 = (uint16_t)(y + rows - 1u);
    chunk->bytes = (size_t)rows * (size_t)width * 2u;

    if (rect->x0 == 0 && rect->x1 == GAME_DISPLAY_WIDTH - 1) {
      chunk->data = framebuffer +
        ((size_t)y * GAME_DISPLAY_WIDTH * 2u);
    } else {
#if GAME_ENABLE_PROFILING
      uint32_t copy_started = profile_now();
#endif
      for (uint32_t row = 0; row < rows; row++) {
        const uint8_t *source = framebuffer +
          (((size_t)(y + row) * GAME_DISPLAY_WIDTH) + rect->x0) * 2u;
        memcpy(tft_stripe[buffer_index] +
               (size_t)row * (size_t)width * 2u,
               source, (size_t)width * 2u);
      }
      chunk->data = tft_stripe[buffer_index];
#if GAME_ENABLE_PROFILING
      profile_present_copy_us += profile_now() - copy_started;
#endif
    }

    cursor->y += rows;
    if (cursor->y > rect->y1) {
      cursor->rect_index++;
      cursor->rect_active = false;
    }
    return true;
  }
  return false;
}

static void
tft_pipeline_start_chunk(const struct tft_pipeline_chunk *chunk)
{
  uint8_t command = 0x2c;
  dma_channel_config config;

  tft_select();
  gpio_put(TFT_DC_PIN, 0);
  spi_write_blocking(TFT_SPI, &command, 1);
  gpio_put(TFT_DC_PIN, 1);

  config = dma_channel_get_default_config(tft_dma_channel);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
  channel_config_set_dreq(&config, spi_get_dreq(TFT_SPI, true));
  dma_channel_configure(tft_dma_channel, &config,
                        &spi_get_hw(TFT_SPI)->dr, chunk->data,
                        chunk->bytes, true);
#if GAME_ENABLE_PROFILING
  profile_dirty_rects++;
  profile_dirty_bytes += (uint32_t)chunk->bytes;
  profile_present_dma_submits++;
#endif
}

static void
tft_pipeline_finish_chunk(void)
{
#if GAME_ENABLE_PROFILING
  uint32_t wait_started = profile_now();
  dma_channel_wait_for_finish_blocking(tft_dma_channel);
  profile_present_dma_wait_us += profile_now() - wait_started;
  {
    uint32_t idle_started = profile_now();
    tft_wait_for_idle();
    profile_present_spi_idle_us += profile_now() - idle_started;
  }
#else
  dma_channel_wait_for_finish_blocking(tft_dma_channel);
  tft_wait_for_idle();
#endif
  tft_deselect();
}

static bool
tft_transfer_dirty_rects_pipeline(void)
{
  struct tft_pipeline_cursor cursor = {0, 0, false};
  struct tft_pipeline_chunk current;
  struct tft_pipeline_chunk next;
  uint8_t current_buffer = 0;

  /* Leave the normal synchronous path available if another subsystem has
   * consumed the last DMA channel. */
  if (tft_dma_channel < 0) return false;
  if (!tft_pipeline_next_chunk(&cursor, current_buffer, &current)) {
    return true;
  }

  for (;;) {
#if GAME_ENABLE_PROFILING
    uint32_t window_started = profile_now();
    tft_set_window(current.x0, current.y0, current.x1, current.y1);
    profile_present_window_us += profile_now() - window_started;
#else
    tft_set_window(current.x0, current.y0, current.x1, current.y1);
#endif
    tft_pipeline_start_chunk(&current);

    bool has_next = tft_pipeline_next_chunk(&cursor, current_buffer ^ 1u,
                                            &next);
#if GAME_ENABLE_PROFILING
    if (has_next && !dma_channel_is_busy(tft_dma_channel)) {
      profile_present_pipeline_stalls++;
    }
#endif
    tft_pipeline_finish_chunk();
    if (!has_next) break;
    current = next;
    current_buffer ^= 1u;
  }
  return true;
}
#endif

#if GAME_TFT_CORE1
static void
tft_core1_send_pixels(struct tft_core1_frame_slot *slot,
                      const uint8_t *data, size_t bytes)
{
  uint8_t command = 0x2c;

  tft_select();
  gpio_put(TFT_DC_PIN, 0);
  spi_write_blocking(TFT_SPI, &command, 1);
  gpio_put(TFT_DC_PIN, 1);
  if (tft_dma_channel >= 0) {
    dma_channel_config config = dma_channel_get_default_config(tft_dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, spi_get_dreq(TFT_SPI, true));
    dma_channel_configure(tft_dma_channel, &config,
                          &spi_get_hw(TFT_SPI)->dr, data, bytes, true);
#if GAME_ENABLE_PROFILING
    {
      uint32_t started = time_us_32();
      dma_channel_wait_for_finish_blocking(tft_dma_channel);
      slot->dma_wait_us += time_us_32() - started;
    }
#else
    dma_channel_wait_for_finish_blocking(tft_dma_channel);
#endif
  } else {
    spi_write_blocking(TFT_SPI, data, bytes);
  }
#if GAME_ENABLE_PROFILING
  {
    uint32_t started = time_us_32();
    tft_wait_for_idle();
    slot->spi_idle_us += time_us_32() - started;
  }
#else
  tft_wait_for_idle();
#endif
  tft_deselect();
}

static void
tft_core1_transfer_slot(uint32_t slot_index)
{
  struct tft_core1_frame_slot *slot = &tft_core1_slots[slot_index];

#if GAME_ENABLE_PROFILING
  slot->copy_us = 0;
  slot->window_us = 0;
  slot->dma_wait_us = 0;
  slot->spi_idle_us = 0;
#endif

  for (size_t rect_index = 0; rect_index < slot->rect_count; rect_index++) {
    const struct game_dirty_rect *rect = &slot->rects[rect_index];
    uint32_t width = (uint32_t)rect->x1 - (uint32_t)rect->x0 + 1u;
    uint32_t y = rect->y0;

    while (y <= rect->y1) {
      uint32_t rows = (uint32_t)rect->y1 - y + 1u;
      size_t bytes;
      const uint8_t *data;

      if (rows > TFT_STRIPE_ROWS) rows = TFT_STRIPE_ROWS;
      bytes = (size_t)rows * (size_t)width * 2u;
      if (rect->x0 == 0 && rect->x1 == GAME_DISPLAY_WIDTH - 1) {
        data = slot->framebuffer +
          (size_t)y * (size_t)GAME_DISPLAY_WIDTH * 2u;
      } else {
#if GAME_ENABLE_PROFILING
        uint32_t started = time_us_32();
#endif
        for (uint32_t row = 0; row < rows; row++) {
          const uint8_t *source = slot->framebuffer +
            (((size_t)(y + row) * GAME_DISPLAY_WIDTH) + rect->x0) * 2u;
          memcpy(tft_stripe + (size_t)row * (size_t)width * 2u,
                 source, (size_t)width * 2u);
        }
#if GAME_ENABLE_PROFILING
        slot->copy_us += time_us_32() - started;
#endif
        data = tft_stripe;
      }

#if GAME_ENABLE_PROFILING
      {
        uint32_t started = time_us_32();
        tft_set_window(rect->x0, (uint16_t)y, rect->x1,
                       (uint16_t)(y + rows - 1u));
        slot->window_us += time_us_32() - started;
      }
#else
      tft_set_window(rect->x0, (uint16_t)y, rect->x1,
                     (uint16_t)(y + rows - 1u));
#endif
      tft_core1_send_pixels(slot, data, bytes);
      y += rows;
    }
  }
}

static void
tft_core1_main(void)
{
  for (;;) {
    uint32_t slot_index;

    for (;;) {
      if (tft_core1_slot_states[0] == TFT_CORE1_SLOT_READY) {
        slot_index = 0;
        break;
      }
      if (tft_core1_slot_states[1] == TFT_CORE1_SLOT_READY) {
        slot_index = 1;
        break;
      }
      __wfe();
    }

    __dmb();
    if (tft_core1_slot_states[slot_index] != TFT_CORE1_SLOT_READY) {
      continue;
    }
    tft_core1_slot_states[slot_index] = TFT_CORE1_SLOT_SENDING;
    __dmb();
    tft_core1_transfer_slot(slot_index);
    __dmb();
    tft_core1_slot_states[slot_index] = TFT_CORE1_SLOT_FREE;
    __sev();
  }
}

static bool
tft_core1_start(void)
{
  if (tft_core1_started || tft_dma_channel < 0) {
    return tft_core1_started;
  }
  tft_core1_slots[0].framebuffer = framebuffers[0];
  tft_core1_slots[1].framebuffer = framebuffers[1];
  tft_core1_slots[0].rect_count = 0;
  tft_core1_slots[1].rect_count = 0;
#if GAME_ENABLE_PROFILING
  tft_core1_slots[0].copy_us = 0;
  tft_core1_slots[0].window_us = 0;
  tft_core1_slots[0].dma_wait_us = 0;
  tft_core1_slots[0].spi_idle_us = 0;
  tft_core1_slots[1].copy_us = 0;
  tft_core1_slots[1].window_us = 0;
  tft_core1_slots[1].dma_wait_us = 0;
  tft_core1_slots[1].spi_idle_us = 0;
#endif
  tft_core1_slot_states[0] = TFT_CORE1_SLOT_FREE;
  tft_core1_slot_states[1] = TFT_CORE1_SLOT_FREE;
  __dmb();
  multicore_launch_core1(tft_core1_main);
  tft_core1_started = true;
  printf("TFT: Core 1 presenter started\n");
  return true;
}

static void
tft_core1_wait_for_slot(uint32_t slot_index)
{
  while (tft_core1_slot_states[slot_index] != TFT_CORE1_SLOT_FREE) {
    __wfe();
  }
}

#if GAME_ENABLE_PROFILING
static void
tft_core1_profile_frame(void)
{
  uint32_t chunks = 0;
  uint32_t bytes = 0;

  for (size_t index = 0; index < dirty_rect_count; index++) {
    const struct game_dirty_rect *rect = &dirty_rects[index];
    uint32_t width = (uint32_t)rect->x1 - (uint32_t)rect->x0 + 1u;
    uint32_t rows = (uint32_t)rect->y1 - (uint32_t)rect->y0 + 1u;
    uint32_t chunk_rows = (rows + TFT_STRIPE_ROWS - 1u) / TFT_STRIPE_ROWS;
    chunks += chunk_rows;
    bytes += (uint32_t)((size_t)width * (size_t)rows * 2u);
  }
  profile_dirty_rects = chunks;
  profile_dirty_bytes = bytes;
  profile_present_dma_submits = chunks;
}
#endif

static bool
tft_core1_submit_frame(void)
{
  struct tft_core1_frame_slot *slot;
#if GAME_ENABLE_PROFILING
  uint32_t wait_started;
#endif

  if (!tft_core1_start()) {
    return false;
  }

  slot = &tft_core1_slots[current_framebuffer_index];
#if GAME_ENABLE_PROFILING
  wait_started = time_us_32();
#endif
  if (tft_core1_slot_states[current_framebuffer_index] !=
      TFT_CORE1_SLOT_FREE) {
#if GAME_ENABLE_PROFILING
    profile_present_core1_stalls++;
#endif
    tft_core1_wait_for_slot(current_framebuffer_index);
  }
#if GAME_ENABLE_PROFILING
  profile_present_core1_wait_us += time_us_32() - wait_started;
  profile_present_core1_submits++;
  tft_core1_profile_frame();
#endif
  slot->framebuffer = framebuffer;
  slot->rect_count = dirty_rect_count;
  if (dirty_rect_count > 0) {
    memcpy(slot->rects, dirty_rects,
           dirty_rect_count * sizeof(dirty_rects[0]));
  }
  __dmb();
  tft_core1_slot_states[current_framebuffer_index] = TFT_CORE1_SLOT_READY;
  __sev();
  return true;
}

static void
tft_core1_advance_framebuffer(void)
{
  uint32_t next_index = current_framebuffer_index ^ 1u;
#if GAME_ENABLE_PROFILING
  uint32_t wait_started = time_us_32();
#endif

#if GAME_ENABLE_PROFILING
  if (tft_core1_slot_states[next_index] != TFT_CORE1_SLOT_FREE) {
    profile_present_core1_stalls++;
  }
#endif
  tft_core1_wait_for_slot(next_index);
#if GAME_ENABLE_PROFILING
  profile_present_copy_us += tft_core1_slots[next_index].copy_us;
  profile_present_window_us += tft_core1_slots[next_index].window_us;
  profile_present_dma_wait_us += tft_core1_slots[next_index].dma_wait_us;
  profile_present_spi_idle_us += tft_core1_slots[next_index].spi_idle_us;
  profile_present_core1_wait_us += time_us_32() - wait_started;
#endif
  current_framebuffer_index = (uint8_t)next_index;
  framebuffer = framebuffers[next_index];
}
#endif
#endif

bool
game_display_present(void)
{
#if !GAME_ENABLE_DIRTY_RECT && !GAME_FRAMEBUFFER_IN_SRAM && !GAME_TFT_DMA_FROM_PSRAM
  size_t offset;
#endif
#if !GAME_ENABLE_DIRTY_RECT
  uint8_t command;
#endif
#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif

  if (!tft_ready) {
    return false;
  }
  game_3d_frame_active = false;

#if GAME_ENABLE_DIRTY_RECT
#if GAME_ENABLE_PROFILING
  profile_dirty_rects = 0;
  profile_dirty_bytes = 0;
#endif
#if GAME_ENABLE_FRAME_DIFF
  if (frame_diff_ready) {
#if GAME_ENABLE_PROFILING
    uint32_t diff_started = profile_start();
#endif
    display_diff_build();
#if GAME_ENABLE_PROFILING
    profile_add(&profile_diff_us, diff_started);
#endif
  }
#endif
#if GAME_TFT_CORE1
  if (!tft_core1_submit_frame()) {
    for (size_t index = 0; index < dirty_rect_count; index++) {
      tft_transfer_dirty_rect(&dirty_rects[index]);
    }
  }
#elif GAME_TFT_PIPELINE
  if (!tft_transfer_dirty_rects_pipeline()) {
    for (size_t index = 0; index < dirty_rect_count; index++) {
      tft_transfer_dirty_rect(&dirty_rects[index]);
    }
  }
#else
  for (size_t index = 0; index < dirty_rect_count; index++) {
    tft_transfer_dirty_rect(&dirty_rects[index]);
  }
#endif
#if GAME_ENABLE_FRAME_DIFF
  if (frame_diff_ready) {
#if GAME_ENABLE_PROFILING
    uint32_t diff_started = profile_start();
#endif
    display_diff_commit();
#if GAME_ENABLE_PROFILING
    profile_add(&profile_diff_us, diff_started);
#endif
  }
#endif
#if GAME_ENABLE_BACKGROUND_LAYER
  background_finish_frame();
#endif
  display_dirty_reset();
#if GAME_TFT_CORE1
  if (tft_core1_started) {
    tft_core1_advance_framebuffer();
  }
#endif
#elif GAME_FRAMEBUFFER_IN_SRAM || GAME_TFT_DMA_FROM_PSRAM
#if GAME_ENABLE_PROFILING
  profile_dirty_rects = 1;
  profile_dirty_bytes = GAME_FRAMEBUFFER_BYTES;
#endif
  tft_set_window(0, 0, GAME_DISPLAY_WIDTH - 1, GAME_DISPLAY_HEIGHT - 1);
  tft_select();
  command = 0x2c;
  gpio_put(TFT_DC_PIN, 0);
  spi_write_blocking(TFT_SPI, &command, 1);
  gpio_put(TFT_DC_PIN, 1);

  if (tft_dma_channel >= 0) {
    dma_channel_config config = dma_channel_get_default_config(tft_dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, spi_get_dreq(TFT_SPI, true));
    dma_channel_configure(tft_dma_channel, &config,
                          &spi_get_hw(TFT_SPI)->dr,
                          framebuffer, GAME_FRAMEBUFFER_BYTES, true);
    dma_channel_wait_for_finish_blocking(tft_dma_channel);
  } else {
    spi_write_blocking(TFT_SPI, framebuffer, GAME_FRAMEBUFFER_BYTES);
  }
  tft_wait_for_idle();
  tft_deselect();
#else
#if GAME_ENABLE_PROFILING
  profile_dirty_rects = GAME_DISPLAY_HEIGHT / TFT_STRIPE_ROWS +
                        (GAME_DISPLAY_HEIGHT % TFT_STRIPE_ROWS != 0);
  profile_dirty_bytes = GAME_FRAMEBUFFER_BYTES;
#endif
  /* Copy small stripes through SRAM before each transfer. This avoids
   * depending on direct DMA reads from the PSRAM XIP window and also keeps
   * the blocking-SPI fallback safe on boards where no DMA channel is free. */
  for (uint32_t y = 0; y < GAME_DISPLAY_HEIGHT; y += TFT_STRIPE_ROWS) {
    uint32_t rows = GAME_DISPLAY_HEIGHT - y;
    if (rows > TFT_STRIPE_ROWS) rows = TFT_STRIPE_ROWS;
    size_t bytes = (size_t)rows * GAME_DISPLAY_WIDTH * 2u;
    offset = (size_t)y * GAME_DISPLAY_WIDTH * 2u;
    memcpy(tft_stripe, framebuffer + offset, bytes);

    tft_set_window(0, (uint16_t)y, GAME_DISPLAY_WIDTH - 1,
                   (uint16_t)(y + rows - 1));
    tft_select();
    command = 0x2c;
    gpio_put(TFT_DC_PIN, 0);
    spi_write_blocking(TFT_SPI, &command, 1);
    gpio_put(TFT_DC_PIN, 1);
    if (tft_dma_channel >= 0) {
      dma_channel_config config = dma_channel_get_default_config(tft_dma_channel);
      channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
      channel_config_set_dreq(&config, spi_get_dreq(TFT_SPI, true));
      dma_channel_configure(tft_dma_channel, &config,
                            &spi_get_hw(TFT_SPI)->dr,
                            tft_stripe, bytes, true);
      dma_channel_wait_for_finish_blocking(tft_dma_channel);
    } else {
      spi_write_blocking(TFT_SPI, tft_stripe, bytes);
    }
    tft_wait_for_idle();
    tft_deselect();
  }
#endif
#if GAME_ENABLE_PROFILING
  profile_finish_frame(profile_started);
#endif
  return true;
}

bool
game_storage_file_size(const char *path, size_t *size)
{
  char resolved[128];
  FILINFO info;
  FRESULT result;
  if (!resolve_asset_path(path, resolved, sizeof(resolved)) || !sd_ready || size == NULL) {
    printf("SD: asset path rejected path=%s\n", path != NULL ? path : "(null)");
    return false;
  }
  result = f_stat(resolved, &info);
  if (result != FR_OK) {
    printf("SD: f_stat path=%s result=%d\n", resolved, result);
    return false;
  }
  *size = (size_t)info.fsize;
  printf("SD: asset path=%s size=%lu\n", resolved, (unsigned long)*size);
  return true;
}

bool
game_storage_read_file(const char *path, uint8_t *data, size_t capacity, size_t *size)
{
  char resolved[128];
  if (!resolve_asset_path(path, resolved, sizeof(resolved))) {
    return false;
  }
  return read_file_resolved(resolved, data, capacity, size);
}

bool
game_storage_save_supported(void)
{
  return sd_ready;
}

bool
game_storage_save_exists(const char *slot)
{
  size_t size;

  return save_stat_path(slot, NULL, 0, &size);
}

bool
game_storage_save_file_size(const char *slot, size_t *size)
{
  if (size == NULL) {
    return false;
  }
  return save_stat_path(slot, NULL, 0, size);
}

bool
game_storage_save_read(const char *slot, uint8_t *data, size_t capacity,
                       size_t *size)
{
  char resolved[128];
  size_t file_size = 0;
  bool found;

  found = save_stat_path(slot, resolved, sizeof(resolved), &file_size);
  if (!found ||
      file_size > GAME_SAVE_MAX_BYTES || capacity < file_size) {
    if (file_size > GAME_SAVE_MAX_BYTES) {
      printf("SD: save too large slot=%s size=%lu\n",
             slot != NULL ? slot : "(null)", (unsigned long)file_size);
    }
    return false;
  }
  if (!read_file_resolved(resolved, data, capacity, size)) {
    return false;
  }
  return *size == file_size;
}

bool
game_storage_save_write(const char *slot, const uint8_t *data, size_t size)
{
  char target[128];
  char temporary[128];
  char backup[128];
  FILINFO info;
  FRESULT result;
  bool had_target;

  if (!sd_ready || (data == NULL && size > 0) || size > GAME_SAVE_MAX_BYTES ||
      !resolve_save_path(slot, "sav", target, sizeof(target)) ||
      !resolve_save_path(slot, "tmp", temporary, sizeof(temporary)) ||
      !resolve_save_path(slot, "bak", backup, sizeof(backup))) {
    printf("SD: save write rejected slot=%s size=%lu\n",
           slot != NULL ? slot : "(null)", (unsigned long)size);
    return false;
  }
  if (!save_directory_ready() || !save_recover_backup(target, backup) ||
      !save_unlink_if_present(temporary)) {
    return false;
  }
  if (!save_write_file(temporary, data, size)) {
    save_unlink_if_present(temporary);
    return false;
  }

  result = f_stat(target, &info);
  if (result == FR_OK) {
    had_target = true;
  } else if (save_path_missing(result)) {
    had_target = false;
  } else {
    printf("SD: save target stat path=%s result=%d\n", target, result);
    save_unlink_if_present(temporary);
    return false;
  }

  if (had_target) {
    result = f_rename(target, backup);
    if (result != FR_OK) {
      printf("SD: save backup rename failed result=%d\n", result);
      save_unlink_if_present(temporary);
      return false;
    }
  }
  result = f_rename(temporary, target);
  if (result != FR_OK) {
    printf("SD: save commit rename failed result=%d\n", result);
    if (had_target) {
      f_rename(backup, target);
    }
    save_unlink_if_present(temporary);
    return false;
  }
  if (had_target && !save_unlink_if_present(backup)) {
    /* The committed save is valid; a leftover backup will be cleaned up on
     * the next save or recovered if power was lost during the replacement. */
    printf("SD: save backup cleanup deferred\n");
  }
  printf("SD: save slot=%s bytes=%lu\n", slot, (unsigned long)size);
  return true;
}

bool
game_storage_save_delete(const char *slot)
{
  char target[128];
  char temporary[128];
  char backup[128];

  if (!sd_ready || !resolve_save_path(slot, "sav", target, sizeof(target)) ||
      !resolve_save_path(slot, "tmp", temporary, sizeof(temporary)) ||
      !resolve_save_path(slot, "bak", backup, sizeof(backup))) {
    return false;
  }
  return save_unlink_if_present(target) &&
         save_unlink_if_present(temporary) &&
         save_unlink_if_present(backup);
}

bool
game_storage_load_rom(uint8_t **data, size_t *size)
{
  FILINFO info;
  size_t rom_size;
  size_t loaded;
  uint8_t *rom_buffer;

  if (!sd_ready || data == NULL || size == NULL ||
      f_stat("sd:/" GAME_ROM_FILENAME, &info) != FR_OK) {
    return false;
  }
  rom_size = (size_t)info.fsize;
  if (rom_size == 0) {
    return false;
  }

  rom_buffer = (uint8_t *)media_alloc(rom_size, 8u);
  if (rom_buffer == NULL) {
    printf("SD: " GAME_ROM_FILENAME
           " does not fit in PSRAM media area (%lu bytes)\n",
           (unsigned long)rom_size);
    return false;
  }
  loaded = 0;
  if (!read_file_resolved("sd:/" GAME_ROM_FILENAME, rom_buffer, rom_size,
                          &loaded)) {
    return false;
  }
  *data = rom_buffer;
  *size = loaded;
  return true;
}

static uint16_t
audio_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t
audio_le32(const uint8_t *data)
{
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static bool
audio_parse_wav(const uint8_t *wav, size_t wav_size,
                const uint8_t **sample_data, size_t *sample_size,
                uint32_t *sample_rate)
{
  size_t offset = 12;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
  uint32_t rate = 0;
  bool found_format = false;
  bool found_data = false;

  if (wav == NULL || wav_size < 12 || memcmp(wav, "RIFF", 4) != 0 ||
      memcmp(wav + 8, "WAVE", 4) != 0) {
    return false;
  }

  while (offset + 8 <= wav_size) {
    const uint8_t *chunk = wav + offset;
    uint32_t chunk_size = audio_le32(chunk + 4);
    size_t chunk_data = offset + 8;
    size_t next_offset;

    if (chunk_size > wav_size - chunk_data) {
      return false;
    }
    if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
      audio_format = audio_le16(wav + chunk_data);
      channels = audio_le16(wav + chunk_data + 2);
      rate = audio_le32(wav + chunk_data + 4);
      bits_per_sample = audio_le16(wav + chunk_data + 14);
      found_format = true;
    } else if (memcmp(chunk, "data", 4) == 0) {
      *sample_data = wav + chunk_data;
      *sample_size = (size_t)chunk_size;
      found_data = true;
    }

    next_offset = chunk_data + (size_t)chunk_size + (chunk_size & 1u);
    if (next_offset > wav_size) {
      return false;
    }
    offset = next_offset;
  }

  if (!found_format || !found_data || audio_format != 1 || channels != 1 ||
      bits_per_sample != 8 || rate == 0 || rate > 96000 || *sample_size == 0) {
    return false;
  }
  *sample_rate = rate;
  return true;
}

static size_t
audio_fade_samples(const struct game_audio_sound *sound)
{
  size_t fade_samples;

  if (sound == NULL || sound->sample_size < 2) return 1;
  fade_samples = ((size_t)sound->sample_rate * GAME_AUDIO_FADE_MS + 999u) /
                 1000u;
  if (fade_samples < 1) fade_samples = 1;
  if (fade_samples > sound->sample_size / 2u) {
    fade_samples = sound->sample_size / 2u;
  }
  return fade_samples;
}

static uint8_t
audio_envelope_gain(const struct game_audio_voice *voice)
{
  size_t fade_samples;
  size_t position;
  size_t remaining;
  uint32_t gain = UINT8_MAX;
  uint32_t edge_gain;

  if (voice == NULL || voice->sound == NULL ||
      voice->position >= voice->sound->sample_size) {
    return 0;
  }
  fade_samples = audio_fade_samples(voice->sound);
  position = voice->position;
  remaining = voice->sound->sample_size - position;
  if (position < fade_samples) {
    gain = (uint32_t)(position * UINT8_MAX) / fade_samples;
  }
  if (remaining < fade_samples) {
    edge_gain = (uint32_t)(remaining * UINT8_MAX) / fade_samples;
    if (edge_gain < gain) gain = edge_gain;
  }
  return (uint8_t)gain;
}

static bool
audio_mix_sample(int32_t *sample)
{
  bool any_sample = false;
  int mixed = 0;
  for (uint voice_index = 0; voice_index < GAME_AUDIO_MAX_VOICES;
       voice_index++) {
    struct game_audio_voice *voice = &audio_voices[voice_index];
    if (!voice->playing || voice->sound == NULL ||
        voice->position >= voice->sound->sample_size) {
      voice->playing = false;
      continue;
    }
    uint8_t envelope = voice_index == GAME_AUDIO_BGM_VOICE && audio_bgm_loop
                         ? UINT8_MAX : audio_envelope_gain(voice);
    int effective_volume =
      ((int)voice->volume * envelope + UINT8_MAX / 2) / UINT8_MAX;
    /* The midpoint is an unsigned configuration constant. Convert before
     * subtraction so negative PCM stays signed through multiply/divide. */
    int centered = (int)voice->sound->sample_data[voice->position] -
                   (int)GAME_AUDIO_MIDDLE_LEVEL;
    /* Fixed headroom: BGM receives half the range, each SE one eighth. */
    int divisor = voice_index == GAME_AUDIO_BGM_VOICE ? 2 : 8;
    mixed += centered * effective_volume / ((int)UINT8_MAX * divisor);
    voice->position++;
    any_sample = true;
    if (voice->position >= voice->sound->sample_size) {
      if (voice_index == GAME_AUDIO_BGM_VOICE && audio_bgm_loop) voice->position = 0;
      else voice->playing = false;
    }
  }
  if (mixed < -(int)GAME_AUDIO_MIDDLE_LEVEL) {
    mixed = -(int)GAME_AUDIO_MIDDLE_LEVEL;
  }
  if (mixed > (int)UINT8_MAX - (int)GAME_AUDIO_MIDDLE_LEVEL) {
    mixed = (int)UINT8_MAX - (int)GAME_AUDIO_MIDDLE_LEVEL;
  }
  *sample = mixed;
  return any_sample;
}

#if GAME_AUDIO_I2S
static bool
audio_i2s_read_sample(int32_t *sample)
{
  bool active = audio_mix_sample(sample);
  *sample *= 256;
  return active;
}
#endif

static bool
audio_fill_buffer(uint index)
{
  if (index > 1) return false;
  bool any_sample = false;
  const uint8_t master = game_audio_master_volume();
  for (size_t i = 0; i < GAME_AUDIO_BUFFER_SAMPLES; i++) {
#if GAME_AUDIO_I2S
    bool active;
    uint32_t word = game_audio_i2s_word(
      &audio_resampler, audio_sample_rate, audio_i2s_read_sample, &active);
    int32_t sample = (int16_t)(word & 0xffffu);
    uint16_t scaled = (uint16_t)(int16_t)game_audio_limit_output(game_audio_apply_volume(sample, master));
    audio_dma_buffers[index][i] = ((uint32_t)scaled << 16) | scaled;
    game_audio_pcm_probe_sample(&audio_pcm_probe, (int16_t)scaled);
#else
    int32_t sample;
    bool active = audio_mix_sample(&sample);
    audio_dma_buffers[index][i] = (uint8_t)(game_audio_limit_output(game_audio_apply_volume(sample, master)) + GAME_AUDIO_MIDDLE_LEVEL);
#endif
    any_sample |= active;
  }
  return any_sample;
}

#if GAME_AUDIO_I2S
static void
audio_i2s_stop(void)
{
  if (audio_sm < 0) return;
  pio_sm_set_enabled(audio_pio, (uint)audio_sm, false);
  pio_sm_clear_fifos(audio_pio, (uint)audio_sm);
  pio_sm_restart(audio_pio, (uint)audio_sm);
  pio_sm_clkdiv_restart(audio_pio, (uint)audio_sm);
  pio_sm_set_pins_with_mask(audio_pio, (uint)audio_sm, 0,
    (1u << GAME_AUDIO_I2S_DATA_PIN) | (3u << GAME_AUDIO_I2S_BCLK_PIN));
  pio_sm_exec(audio_pio, (uint)audio_sm,
              pio_encode_jmp(audio_program_offset + game_audio_i2s_offset_entry));
  memset(&audio_resampler, 0, sizeof(audio_resampler));
}

static bool
audio_i2s_init(void)
{
  if (!pio_can_add_program(audio_pio, &game_audio_i2s_program)) return false;
  audio_sm = pio_claim_unused_sm(audio_pio, false);
  if (audio_sm < 0) return false;
  audio_program_offset = pio_add_program(audio_pio, &game_audio_i2s_program);
  pio_sm_config config = game_audio_i2s_program_get_default_config(audio_program_offset);
  sm_config_set_out_pins(&config, GAME_AUDIO_I2S_DATA_PIN, 1);
  sm_config_set_sideset_pins(&config, GAME_AUDIO_I2S_BCLK_PIN);
  sm_config_set_out_shift(&config, false, true, 32);
  sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&config, (float)clock_get_hz(clk_sys) /
                                  (GAME_AUDIO_I2S_RATE * 64u));
  pio_gpio_init(audio_pio, GAME_AUDIO_I2S_DATA_PIN);
  pio_gpio_init(audio_pio, GAME_AUDIO_I2S_BCLK_PIN);
  pio_gpio_init(audio_pio, GAME_AUDIO_I2S_BCLK_PIN + 1);
  pio_sm_init(audio_pio, (uint)audio_sm, audio_program_offset, &config);
  pio_sm_set_consecutive_pindirs(audio_pio, (uint)audio_sm, GAME_AUDIO_I2S_DATA_PIN, 1, true);
  pio_sm_set_consecutive_pindirs(audio_pio, (uint)audio_sm, GAME_AUDIO_I2S_BCLK_PIN, 2, true);
  audio_i2s_stop();
  return true;
}
#else
static void
audio_pwm_set_level(uint16_t level)
{
  pwm_set_chan_level(audio_pwm_slice, audio_pwm_channel, level);
}

static uint16_t
audio_pwm_get_level(void)
{
  uint32_t compare = pwm_hw->slice[audio_pwm_slice].cc;

  if (audio_pwm_channel == PWM_CHAN_A) {
    return (uint16_t)(compare & UINT8_MAX);
  }
  return (uint16_t)((compare >> 16) & UINT8_MAX);
}

static void
audio_ramp_to_middle(void)
{
  uint16_t start_level = audio_pwm_get_level();

  for (uint step = 1; step <= GAME_AUDIO_RAMP_STEPS; step++) {
    int level = (int)start_level +
                ((int)GAME_AUDIO_MIDDLE_LEVEL - (int)start_level) *
                  (int)step / (int)GAME_AUDIO_RAMP_STEPS;
    audio_pwm_set_level((uint16_t)level);
    sleep_us((GAME_AUDIO_FADE_MS * 1000u) / GAME_AUDIO_RAMP_STEPS);
  }
  audio_pwm_set_level(GAME_AUDIO_MIDDLE_LEVEL);
}

/* Pick the closest DMA timer fraction representable by its 16-bit fields.
 * Keeping the denominator fixed at UINT16_MAX makes low audio rates coarse:
 * at a 150 MHz system clock, 22.05 kHz becomes 10/65535 (about 22.89 kHz).
 * Searching the small set of useful numerators lets 22.05 kHz use a much
 * closer fraction such as 9/61224. */
static void
audio_select_dma_timer_fraction(uint32_t system_clock,
                                uint32_t sample_rate,
                                uint16_t *numerator,
                                uint16_t *denominator)
{
  uint32_t best_numerator = 1u;
  uint32_t best_denominator = UINT16_MAX;
  uint64_t best_error =
    (uint64_t)system_clock > (uint64_t)sample_rate * best_denominator
      ? (uint64_t)system_clock -
          (uint64_t)sample_rate * best_denominator
      : (uint64_t)sample_rate * best_denominator - system_clock;
  uint64_t max_numerator =
    ((uint64_t)UINT16_MAX * sample_rate) / system_clock;

  if (max_numerator < 1u) max_numerator = 1u;
  if (max_numerator > UINT16_MAX) max_numerator = UINT16_MAX;

  for (uint32_t candidate_numerator = 1u;
       candidate_numerator <= (uint32_t)max_numerator;
       candidate_numerator++) {
    uint64_t scaled_clock =
      (uint64_t)system_clock * candidate_numerator;
    uint32_t candidate_denominator =
      (uint32_t)((scaled_clock + sample_rate / 2u) / sample_rate);
    uint64_t target;
    uint64_t error;

    if (candidate_denominator < candidate_numerator) {
      candidate_denominator = candidate_numerator;
    }
    if (candidate_denominator > UINT16_MAX) continue;

    target = (uint64_t)sample_rate * candidate_denominator;
    error = scaled_clock > target ? scaled_clock - target : target - scaled_clock;
    if (error * best_denominator < best_error * candidate_denominator) {
      best_numerator = candidate_numerator;
      best_denominator = candidate_denominator;
      best_error = error;
    }
  }

  *numerator = (uint16_t)best_numerator;
  *denominator = (uint16_t)best_denominator;
}
#endif

#if GAME_AUDIO_I2S
#if GAME_AUDIO_I2S_CHAIN_DMA
static void
audio_i2s_abort_dma(void)
{
  if (audio_dma_channel < 0 || audio_dma_peer < 0) return;
  dma_channel_set_irq0_enabled((uint)audio_dma_channel, false);
  dma_channel_set_irq0_enabled((uint)audio_dma_peer, false);
  /* Stop both chain links before either abort, preventing a peer restart. */
  dma_channel_config first = dma_get_channel_config((uint)audio_dma_channel);
  dma_channel_config second = dma_get_channel_config((uint)audio_dma_peer);
  channel_config_set_chain_to(&first, (uint)audio_dma_channel);
  channel_config_set_chain_to(&second, (uint)audio_dma_peer);
  dma_channel_set_config((uint)audio_dma_channel, &first, false);
  dma_channel_set_config((uint)audio_dma_peer, &second, false);
  dma_channel_abort((uint)audio_dma_channel);
  dma_channel_abort((uint)audio_dma_peer);
  dma_hw->ints0 = (1u << (uint)audio_dma_channel) | (1u << (uint)audio_dma_peer);
}
#endif

static void
audio_i2s_check_stall(void)
{
  uint32_t mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + (uint)audio_sm);
  if (audio_pio->fdebug & mask) {
    audio_i2s_stalls++;
    audio_pio->fdebug = mask;
  }
}

static bool
audio_i2s_refill(uint index)
{
  uint32_t started = time_us_32();
  bool valid = audio_fill_buffer(index);
  uint32_t elapsed = time_us_32() - started;
  if (elapsed > audio_i2s_refill_max_us) audio_i2s_refill_max_us = elapsed;
  if (elapsed >= (GAME_AUDIO_BUFFER_SAMPLES * 1000000u) / GAME_AUDIO_I2S_RATE) {
    audio_i2s_late_refills++;
  }
  return valid;
}

/* Called only after stopping, or before starting a new stream. Never print in
 * the audio IRQ: USB output must not consume the refill deadline. */
static void
audio_i2s_report_stats(void)
{
  if (audio_i2s_blocks || audio_i2s_stalls) {
    printf("AUDIO: I2S stats chain=%u blocks=%lu stalls=%lu late_irqs=%lu late_refills=%lu refill_max_us=%lu budget_us=%lu pcm_frames=%lu pcm_fnv1a=%08lx pcm_max_step=%lu\n",
      GAME_AUDIO_I2S_CHAIN_DMA, (unsigned long)audio_i2s_blocks,
      (unsigned long)audio_i2s_stalls, (unsigned long)audio_i2s_late_irqs,
      (unsigned long)audio_i2s_late_refills, (unsigned long)audio_i2s_refill_max_us,
      (unsigned long)((GAME_AUDIO_BUFFER_SAMPLES * 1000000u) / GAME_AUDIO_I2S_RATE),
      (unsigned long)audio_pcm_probe.frames, (unsigned long)audio_pcm_probe.fnv1a,
      (unsigned long)audio_pcm_probe.max_step);
  }
  audio_i2s_blocks = audio_i2s_stalls = audio_i2s_late_irqs = 0;
  audio_i2s_late_refills = audio_i2s_refill_max_us = 0;
  game_audio_pcm_probe_reset(&audio_pcm_probe);
}
#endif

static void
audio_dma_irq_handler(void)
{
  uint32_t mask;
  uint completed;
  uint next;

  if (audio_dma_channel < 0) return;
#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
  mask = (1u << (uint)audio_dma_channel) | (1u << (uint)audio_dma_peer);
  mask &= dma_hw->ints0;
  if (!mask) return;
  dma_hw->ints0 = mask;
  for (completed = 0; completed < 2 && audio_playing; completed++) {
    uint channel = (uint)(completed ? audio_dma_peer : audio_dma_channel);
    if (!(mask & (1u << channel))) continue;
    audio_i2s_blocks++;
    /* Both completions can be pending after a long interrupt delay. Never
     * write a buffer whose DMA channel has already started reading it again.
     * Hardware read rings keep a late replay inside that buffer's SRAM. */
    if (dma_channel_is_busy(channel)) {
      audio_i2s_check_stall();
      audio_i2s_late_irqs++;
      continue;
    }
    next = completed ^ 1u;
    if (!audio_buffer_valid[next]) {
      /* A new voice can arrive after the peer was marked EOF but before
       * this final DMA completes. Rebuild both idle buffers before starting
       * the chain again; never discard the newly registered voice. */
      bool pending = false;
      for (uint i = 0; i < GAME_AUDIO_MAX_VOICES; i++) {
        if (audio_voices[i].playing) { pending = true; break; }
      }
      if (pending) {
        uint next_channel = (uint)(next ? audio_dma_peer : audio_dma_channel);
        if (dma_channel_is_busy(next_channel)) {
          audio_i2s_check_stall();
          audio_i2s_late_irqs++;
          continue;
        }
        audio_buffer_valid[next] = audio_i2s_refill(next);
        audio_buffer_valid[completed] = audio_i2s_refill(completed);
        if (audio_buffer_valid[next]) {
          dma_channel_set_trans_count(channel,
            audio_buffer_valid[completed] ? GAME_AUDIO_BUFFER_SAMPLES : 0, false);
          audio_current_buffer = next;
          dma_channel_set_trans_count(next_channel, GAME_AUDIO_BUFFER_SAMPLES, true);
          continue;
        }
      }
      audio_playing = false;
      audio_sample_rate = 0;
      for (uint i = 0; i < GAME_AUDIO_MAX_VOICES; i++) audio_voices[i].playing = false;
      audio_i2s_abort_dma();
      audio_i2s_stop();
      return;
    }
    /* No further data is expected at EOF; count stalls only while more PCM
     * is queued, so a late final IRQ does not label normal draining a fault. */
    audio_i2s_check_stall();
    audio_current_buffer = next;
    audio_buffer_valid[completed] = audio_i2s_refill(completed);
    if (dma_channel_is_busy(channel)) {
      /* A missed deadline may repeat old/mixed PCM, but cannot read past the
       * ring. Leave the active transfer alone and report it after playback. */
      audio_i2s_late_irqs++;
    } else if (!audio_buffer_valid[completed]) {
      dma_channel_set_trans_count(channel, 0, false);
    }
  }
#else
  mask = 1u << (uint)audio_dma_channel;
  if ((dma_hw->ints0 & mask) == 0) return;
  dma_hw->ints0 = mask;
  if (!audio_playing) return;

  completed = audio_current_buffer;
  next = completed ^ 1u;
  if (!audio_buffer_valid[next]) {
    bool pending = false;
    for (uint i = 0; i < GAME_AUDIO_MAX_VOICES; i++) {
      if (audio_voices[i].playing) { pending = true; break; }
    }
    if (pending) audio_buffer_valid[next] = audio_fill_buffer(next);
  }
  if (!audio_buffer_valid[next]) {
    audio_playing = false;
    audio_sample_rate = 0;
    for (uint voice_index = 0; voice_index < GAME_AUDIO_MAX_VOICES;
         voice_index++) {
      audio_voices[voice_index].playing = false;
    }
    dma_channel_set_irq0_enabled((uint)audio_dma_channel, false);
#if GAME_AUDIO_I2S
    audio_i2s_stop();
#else
    audio_pwm_set_level(GAME_AUDIO_MIDDLE_LEVEL);
#endif
    return;
  }

  audio_current_buffer = next;
  /* Resume the prepared PCM block before mixing the freed buffer. */
  dma_channel_set_read_addr((uint)audio_dma_channel, audio_dma_buffers[next], false);
  dma_channel_set_trans_count((uint)audio_dma_channel,
                              GAME_AUDIO_BUFFER_SAMPLES, true);
#if GAME_AUDIO_I2S
  audio_i2s_blocks++;
  audio_i2s_check_stall();
  audio_buffer_valid[completed] = audio_i2s_refill(completed);
#else
  audio_buffer_valid[completed] = audio_fill_buffer(completed);
#endif
#endif
}

static void
audio_init(void)
{
#if GAME_AUDIO_I2S
  if (!audio_output_ready) {
    if (!audio_i2s_init()) {
      printf("AUDIO: I2S PIO unavailable\n");
      return;
    }
    audio_output_ready = true;
  }
#else
  pwm_config pwm_configuration;

  audio_pwm_slice = pwm_gpio_to_slice_num(GAME_AUDIO_GPIO);
  audio_pwm_channel = pwm_gpio_to_channel(GAME_AUDIO_GPIO);
  gpio_set_function(GAME_AUDIO_GPIO, GPIO_FUNC_PWM);

  if (!audio_output_ready) {
    pwm_configuration = pwm_get_default_config();
    pwm_config_set_wrap(&pwm_configuration, 255u);
    pwm_config_set_clkdiv(&pwm_configuration, 1.0f);
    pwm_init(audio_pwm_slice, &pwm_configuration, false);
    audio_pwm_set_level(GAME_AUDIO_MIDDLE_LEVEL);
    pwm_set_enabled(audio_pwm_slice, true);
    audio_output_ready = true;
  }
#endif

  if (audio_dma_channel >= 0) return;
  audio_dma_channel = dma_claim_unused_channel(false);
  if (audio_dma_channel < 0) {
    printf("AUDIO: no DMA channel available\n");
    return;
  }
#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
  audio_dma_peer = dma_claim_unused_channel(false);
  if (audio_dma_peer < 0) {
    dma_channel_unclaim((uint)audio_dma_channel);
    audio_dma_channel = -1;
    printf("AUDIO: second I2S DMA channel unavailable\n");
    return;
  }
#endif
#if !GAME_AUDIO_I2S
  audio_dma_timer = dma_claim_unused_timer(false);
  if (audio_dma_timer < 0) {
    dma_channel_unclaim((uint)audio_dma_channel);
    audio_dma_channel = -1;
    printf("AUDIO: no DMA timer available\n");
    return;
  }
#endif
  irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);
#if GAME_AUDIO_I2S
  printf("AUDIO: I2S BCLK=GP%u WS=GP%u DATA=GP%u rate=%u bits=16 stereo=mono PIO=0 SM=%d DMA=%d chain=%u peer=%d math=%u pcm=signed\n",
    GAME_AUDIO_I2S_BCLK_PIN, GAME_AUDIO_I2S_BCLK_PIN + 1,
    GAME_AUDIO_I2S_DATA_PIN, GAME_AUDIO_I2S_RATE, audio_sm, audio_dma_channel,
    GAME_AUDIO_I2S_CHAIN_DMA, audio_dma_peer, GAME_AUDIO_I2S_WIDE_MATH ? 64u : 32u);
#else
  printf("AUDIO: GP%u PWM slice=%u channel=%u DMA=%d timer=%d pcm=signed\n",
         GAME_AUDIO_GPIO, audio_pwm_slice, audio_pwm_channel,
         audio_dma_channel, audio_dma_timer);
#endif
  printf("AUDIO: max_gain=%u%%\n", (unsigned)GAME_AUDIO_MAX_GAIN);
}

static void
audio_boot_chime(void)
{
  static uint8_t samples[GAME_BOOT_CHIME_SAMPLES];
  static game_audio_sound_t sound;
  game_boot_chime_generate(samples);
  sound.sample_data = samples;
  sound.sample_size = sizeof(samples);
  sound.sample_rate = GAME_BOOT_CHIME_RATE;
  if (!game_audio_sound_play(&sound, 128)) return;
  uint32_t started = time_us_32();
  while (game_audio_busy() && (uint32_t)(time_us_32() - started) < 700000u) {
    sleep_ms(1);
  }
  /* Do not let a startup voice interfere with the cartridge's sample rate. */
  if (game_audio_busy()) game_audio_stop();
}

void
game_audio_stop(void)
{
  uint32_t irq_state = save_and_disable_interrupts();
#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
  audio_i2s_abort_dma();
#else
  if (audio_dma_channel >= 0) {
    dma_channel_set_irq0_enabled((uint)audio_dma_channel, false);
    dma_channel_abort((uint)audio_dma_channel);
    dma_hw->ints0 = 1u << (uint)audio_dma_channel;
  }
#endif
  audio_playing = false;
  audio_sample_rate = 0;
  audio_buffer_valid[0] = false;
  audio_buffer_valid[1] = false;
  for (uint voice_index = 0; voice_index < GAME_AUDIO_MAX_VOICES;
       voice_index++) {
    audio_voices[voice_index].sound = NULL;
    audio_voices[voice_index].position = 0;
    audio_voices[voice_index].volume = 0;
    audio_voices[voice_index].playing = false;
  }
  restore_interrupts(irq_state);
#if GAME_AUDIO_I2S
  if (audio_output_ready) audio_i2s_stop();
  audio_i2s_report_stats();
#else
  if (audio_output_ready) audio_ramp_to_middle();
#endif
}

static bool
audio_sound_play_kind(game_audio_sound_t *sound, uint8_t volume, bool bgm, bool loop)
{
#if !GAME_AUDIO_I2S
  volatile uint8_t *pwm_level;
  uint32_t system_clock;
  uint16_t timer_denominator;
  uint16_t timer_numerator;
#endif
  dma_channel_config dma_configuration;
  uint voice_index;
  bool was_playing;

  if (sound == NULL || audio_dma_channel < 0 || !audio_output_ready ||
      sound->sample_data == NULL || sound->sample_size == 0 ||
      sound->sample_rate == 0 || sound->sample_rate > 96000) {
    return false;
  }
  /* DMA completion can retire the last voice. Inspect and publish the new
   * voice atomically with respect to that interrupt. */
  uint32_t irq_state = save_and_disable_interrupts();
  was_playing = audio_playing;
  voice_index = bgm ? GAME_AUDIO_BGM_VOICE : GAME_AUDIO_MAX_VOICES;
  if (!bgm) {
    for (uint candidate = 0; candidate < GAME_AUDIO_SE_VOICES; candidate++) {
      if (!audio_voices[candidate].playing) { voice_index = candidate; break; }
    }
    if (voice_index == GAME_AUDIO_MAX_VOICES) {
      restore_interrupts(irq_state);
      return true; /* Preserve existing SE when all four slots are occupied. */
    }
  }
  if (was_playing && audio_sample_rate != sound->sample_rate) {
    uint32_t active_rate = audio_sample_rate;
    restore_interrupts(irq_state);
    printf("AUDIO: sample rate mismatch active=%lu requested=%lu\n",
           (unsigned long)active_rate,
           (unsigned long)sound->sample_rate);
    return false;
  }

  if (bgm) audio_bgm_loop = loop;
  audio_voices[voice_index].sound = sound;
  audio_voices[voice_index].position = 0;
  audio_voices[voice_index].volume = volume;
  audio_voices[voice_index].playing = true;
  restore_interrupts(irq_state);
  if (was_playing) return true;

  audio_sample_rate = sound->sample_rate;
#if GAME_AUDIO_I2S
  audio_i2s_report_stats();
  audio_i2s_stop();
#endif
  audio_buffer_valid[0] = audio_fill_buffer(0);
  audio_buffer_valid[1] = audio_fill_buffer(1);
  if (!audio_buffer_valid[0]) {
    game_audio_stop();
    return false;
  }

#if !GAME_AUDIO_I2S
  /* Keep a high PWM carrier and use a DMA timer for the actual PCM rate.
   * This avoids feeding the PAM8302 a 44.1 kHz PWM carrier, which would
   * leave too little room for the audio band after filtering. */
  system_clock = clock_get_hz(clk_sys);
  audio_select_dma_timer_fraction(system_clock, sound->sample_rate,
                                  &timer_numerator, &timer_denominator);
  dma_timer_set_fraction((uint)audio_dma_timer, timer_numerator,
                         timer_denominator);

  /* RP2350 normally replicates narrow writes across the whole peripheral
   * register. Use the narrow-write alias so each 8-bit DMA transfer updates
   * only the addressed byte lane of the PWM compare register. */
  pwm_level = (volatile uint8_t *)
    ((uintptr_t)&pwm_hw->slice[audio_pwm_slice].cc + 0x4000u);
  if (audio_pwm_channel != PWM_CHAN_A) {
    pwm_level += 2;
  }
#endif
  dma_configuration = dma_channel_get_default_config((uint)audio_dma_channel);
#if GAME_AUDIO_I2S
  channel_config_set_transfer_data_size(&dma_configuration, DMA_SIZE_32);
#else
  channel_config_set_transfer_data_size(&dma_configuration, DMA_SIZE_8);
#endif
  channel_config_set_read_increment(&dma_configuration, true);
  channel_config_set_write_increment(&dma_configuration, false);
#if GAME_AUDIO_I2S
  channel_config_set_dreq(&dma_configuration, pio_get_dreq(audio_pio, (uint)audio_sm, true));
#else
  channel_config_set_dreq(&dma_configuration,
                          dma_get_timer_dreq((uint)audio_dma_timer));
#endif
#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
  /* Each channel wraps within its own 2KiB buffer. A late CPU cannot cause a
   * DMA read beyond the PCM allocation while hardware keeps the I2S clock live. */
  channel_config_set_ring(&dma_configuration, false, 11);
  channel_config_set_chain_to(&dma_configuration, (uint)audio_dma_channel);
  dma_channel_set_irq0_enabled((uint)audio_dma_peer, true);
  dma_channel_configure((uint)audio_dma_peer, &dma_configuration,
                        &audio_pio->txf[audio_sm], audio_dma_buffers[1],
                        audio_buffer_valid[1] ? GAME_AUDIO_BUFFER_SAMPLES : 0, false);
  channel_config_set_chain_to(&dma_configuration, (uint)audio_dma_peer);
#endif
  audio_current_buffer = 0;
  audio_playing = true;
  dma_channel_set_irq0_enabled((uint)audio_dma_channel, true);
  dma_channel_configure((uint)audio_dma_channel, &dma_configuration,
#if GAME_AUDIO_I2S
                        &audio_pio->txf[audio_sm], audio_dma_buffers[0],
#else
                        (volatile void *)pwm_level, audio_dma_buffers[0],
#endif
                        GAME_AUDIO_BUFFER_SAMPLES, true);
#if GAME_AUDIO_I2S
  /* Start the clock only after DMA has supplied the first word. */
  while (pio_sm_is_tx_fifo_empty(audio_pio, (uint)audio_sm)) tight_loop_contents();
  audio_pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + (uint)audio_sm);
  pio_sm_set_enabled(audio_pio, (uint)audio_sm, true);
  printf("AUDIO: playing source_rate=%lu output_rate=%u voices=%u I2S\n",
         (unsigned long)sound->sample_rate, GAME_AUDIO_I2S_RATE, GAME_AUDIO_MAX_VOICES);
#else
  printf("AUDIO: playing rate=%lu voices=%u timer=%u/%u\n",
         (unsigned long)sound->sample_rate, GAME_AUDIO_MAX_VOICES,
         timer_numerator, timer_denominator);
#endif
  return true;
}

bool
game_audio_sound_play(game_audio_sound_t *sound, uint8_t volume)
{
  if (sound == NULL || sound->wav_bytes > GAME_AUDIO_MAX_SE_BYTES) return false;
  return audio_sound_play_kind(sound, volume, false, false);
}

bool
game_audio_sound_play_bgm(game_audio_sound_t *sound, uint8_t volume, bool loop)
{
  return audio_sound_play_kind(sound, volume, true, loop);
}

#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
/* A late DMA interrupt can leave playback marked active after the chained
 * transfers stop making progress. Check on Core 0, outside the audio IRQ.
 * Restart the BGM (or one remaining SE) after a sustained interruption. */
static void
audio_i2s_watchdog(void)
{
  static uint32_t last_blocks;
  static uint32_t last_progress_us;
  static bool watching;
  uint32_t now = time_us_32();
  uint32_t state = save_and_disable_interrupts();
  uint32_t blocks = audio_i2s_blocks;
  if (!audio_playing) {
    watching = false;
    restore_interrupts(state);
    return;
  }
  if (!watching || blocks != last_blocks) {
    watching = true;
    last_blocks = blocks;
    last_progress_us = now;
    restore_interrupts(state);
    return;
  }
  if ((uint32_t)(now - last_progress_us) < 250000u) {
    restore_interrupts(state);
    return;
  }

  uint voice_index = GAME_AUDIO_BGM_VOICE;
  if (!audio_voices[voice_index].playing || audio_voices[voice_index].sound == NULL) {
    for (voice_index = 0; voice_index < GAME_AUDIO_SE_VOICES; voice_index++) {
      if (audio_voices[voice_index].playing && audio_voices[voice_index].sound != NULL) break;
    }
    if (voice_index == GAME_AUDIO_SE_VOICES) voice_index = GAME_AUDIO_MAX_VOICES;
  }
  const game_audio_sound_t *sound = voice_index < GAME_AUDIO_MAX_VOICES
    ? audio_voices[voice_index].sound : NULL;
  uint8_t volume = sound != NULL ? audio_voices[voice_index].volume : 0;
  bool bgm = sound != NULL && voice_index == GAME_AUDIO_BGM_VOICE;
  bool loop = bgm && audio_bgm_loop;
  bool first_busy = dma_channel_is_busy((uint)audio_dma_channel);
  bool second_busy = dma_channel_is_busy((uint)audio_dma_peer);
  watching = false;
  restore_interrupts(state);

  printf("AUDIO: I2S watchdog no DMA progress for 250ms dma_busy=%u/%u bgm=%u\n",
         first_busy, second_busy, bgm);
  game_audio_stop();
  if (sound != NULL && !audio_sound_play_kind((game_audio_sound_t *)sound, volume, bgm, loop)) {
    printf("AUDIO: I2S watchdog restart failed\n");
  }
}
#endif

void
game_audio_bgm_stop(void)
{
  uint32_t state = save_and_disable_interrupts();
  audio_voices[GAME_AUDIO_BGM_VOICE].playing = false;
  restore_interrupts(state);
}

game_audio_sound_t *
game_audio_sound_load(const char *path)
{
  size_t file_size;
  size_t loaded;
  const uint8_t *sample_data;
  size_t sample_size;
  uint32_t sample_rate;
  game_audio_sound_t *sound;

  if (path == NULL || !sd_ready || !game_storage_file_size(path, &file_size) ||
      file_size == 0 || file_size > GAME_AUDIO_MAX_WAV_BYTES) {
    printf("AUDIO: file rejected path=%s\n", path != NULL ? path : "(null)");
    return NULL;
  }
  sound = (game_audio_sound_t *)asset_alloc(sizeof(*sound));
  if (sound == NULL) {
    printf("AUDIO: sound descriptor allocation failed\n");
    goto load_failed;
  }
  sound->wav_data = (uint8_t *)asset_alloc(file_size);
  if (sound->wav_data == NULL) {
    printf("AUDIO: sample allocation failed path=%s\n", path);
    goto load_failed;
  }
  loaded = 0;
  if (!game_storage_read_file(path, sound->wav_data, file_size, &loaded) ||
      loaded != file_size ||
      !audio_parse_wav(sound->wav_data, loaded, &sample_data, &sample_size,
                       &sample_rate)) {
    printf("AUDIO: unsupported WAV path=%s bytes=%lu\n", path,
           (unsigned long)loaded);
    goto load_failed;
  }
  sound->wav_bytes = loaded;
  sound->sample_data = sample_data;
  sound->sample_size = sample_size;
  sound->sample_rate = sample_rate;
  printf("AUDIO: loaded path=%s rate=%lu data=%lu bytes\n", path,
         (unsigned long)sample_rate, (unsigned long)sample_size);
  asset_resident_bytes += file_size;
  sound->next = loaded_sounds;
  loaded_sounds = sound;
  return sound;

load_failed:
  if (sound != NULL) { asset_free(sound->wav_data); asset_free(sound); }
  return NULL;
}

void

game_audio_sound_stop(game_audio_sound_t *sound)
{
  bool any_playing = false;
  uint32_t irq_state;

  if (sound == NULL) return;
  irq_state = save_and_disable_interrupts();
  for (uint voice_index = 0; voice_index < GAME_AUDIO_MAX_VOICES;
       voice_index++) {
    if (audio_voices[voice_index].sound == sound) {
      audio_voices[voice_index].playing = false;
    }
    if (audio_voices[voice_index].playing) any_playing = true;
  }
  restore_interrupts(irq_state);
  if (!any_playing) game_audio_stop();
}

struct game_audio_cached_sound {
  struct game_audio_cached_sound *next;
  game_audio_sound_t *sound;
  char path[];
};

static struct game_audio_cached_sound *audio_cached_sounds;

game_audio_sound_t *
game_audio_sound_load_cached(const char *path)
{
  struct game_audio_cached_sound *entry;
  char resolved[128];
  if (!resolve_asset_path(path, resolved, sizeof(resolved))) return NULL;
  for (entry = audio_cached_sounds; entry != NULL; entry = entry->next) {
    if (strcmp(entry->path, path) == 0) return entry->sound;
  }
  size_t count = 0;
  for (entry = audio_cached_sounds; entry != NULL; entry = entry->next) count++;
  if (count >= 8u) game_assets_collect();
  entry = (struct game_audio_cached_sound *)asset_alloc(
    sizeof(*entry) + strlen(path) + 1u);
  if (entry == NULL) return NULL;
  entry->sound = game_audio_sound_load(path);
  if (entry->sound == NULL) {
    asset_free(entry);
    return NULL;
  }
  strcpy(entry->path, path);
  entry->next = audio_cached_sounds;
  audio_cached_sounds = entry;
  return entry->sound;
}

void
game_sprite_release(game_sprite_t *sprite)
{
  if (sprite == NULL) return;
  asset_resident_bytes -= sprite->bytes;
  if (sprite->cached) sprite_cache_free(sprite->pixels);
  else asset_free(sprite->pixels);
  asset_free(sprite);
}

void
game_texture_release(game_texture_t *texture)
{
  if (texture == NULL) return;
  asset_resident_bytes -= texture->bytes;
  asset_free(texture->pixels);
  asset_free(texture);
}

void
game_font_release(game_font_t *font)
{
  if (font == NULL) return;
  asset_resident_bytes -= font->bytes;
  asset_free(font->data);
  asset_free(font);
}

/* The IRQ never frees: a collected Sound remains valid until all voices have
 * copied their last samples into the SRAM DMA buffers. Reap on Core 0. */
static bool
audio_sound_in_use(const game_audio_sound_t *sound)
{
  bool active = false;
  uint32_t state = save_and_disable_interrupts();
  for (uint index = 0; index < GAME_AUDIO_MAX_VOICES; index++) {
    if (audio_voices[index].sound == sound && audio_voices[index].playing) active = true;
  }
  restore_interrupts(state);
  return active;
}

void
game_assets_sweep(void)
{
  game_audio_sound_t **link = &loaded_sounds;
  while (*link != NULL) {
    game_audio_sound_t *sound = *link;
    if (!sound->released || audio_sound_in_use(sound)) {
      link = &sound->next;
      continue;
    }
    uint32_t state = save_and_disable_interrupts();
    for (uint index = 0; index < GAME_AUDIO_MAX_VOICES; index++) {
      if (audio_voices[index].sound == sound) audio_voices[index].sound = NULL;
    }
    restore_interrupts(state);
    *link = sound->next;
    asset_resident_bytes -= sound->wav_bytes;
    asset_free(sound->wav_data);
    asset_free(sound);
  }
}

void
game_audio_sound_release(game_audio_sound_t *sound)
{
  if (sound == NULL) return;
  sound->released = true;
  game_assets_sweep();
}

void
game_assets_collect(void)
{
  struct game_audio_cached_sound **link = &audio_cached_sounds;
  while (*link != NULL) {
    struct game_audio_cached_sound *entry = *link;
    if (audio_sound_in_use(entry->sound)) {
      link = &entry->next;
      continue;
    }
    *link = entry->next;
    game_audio_sound_release(entry->sound);
    asset_free(entry);
  }
  game_assets_sweep();
}

size_t
game_assets_resident_bytes(void)
{
  game_assets_sweep();
  return asset_resident_bytes;
}

bool
game_audio_play(const char *path)
{
  game_audio_sound_t *sound = game_audio_sound_load_cached(path);
  return sound != NULL && game_audio_sound_play(sound, UINT8_MAX);
}

bool
game_audio_busy(void)
{
  return audio_playing;
}

uint32_t
game_time_millis(void)
{
  return time_us_32() / 1000u;
}

static void
fill_rgb565_row(uint8_t *destination, size_t pixels, uint16_t color)
{
  uint8_t hi = (uint8_t)(color >> 8);
  uint8_t lo = (uint8_t)color;
  uint32_t pattern = ((uint32_t)lo << 24) | ((uint32_t)hi << 16) |
                     ((uint32_t)lo << 8) | hi;

  /* The framebuffer is 4-byte aligned and each pixel is 2 bytes, so an odd
   * pixel x-coordinate leaves a 2-byte prefix before the word-aligned part. */
  if ((((uintptr_t)destination & 2u) != 0u) && pixels > 0) {
    destination[0] = hi;
    destination[1] = lo;
    destination += 2;
    pixels--;
  }

  uint32_t *words = (uint32_t *)(void *)destination;
  while (pixels >= 2) {
    *words++ = pattern;
    pixels -= 2;
  }
  destination = (uint8_t *)(void *)words;
  if (pixels > 0) {
    destination[0] = hi;
    destination[1] = lo;
  }
}

/* Decode a Unicode scalar; malformed bytes each consume one fallback cell. */
static uint32_t
text_next_utf8(const char **text)
{
  const unsigned char *p = (const unsigned char *)*text;
  uint32_t c = *p++;
  unsigned count = 0;
  uint32_t minimum = 0;
  if (c >= 0xc2 && c <= 0xdf) { c &= 0x1f; count = 1; minimum = 0x80; }
  else if (c >= 0xe0 && c <= 0xef) { c &= 0x0f; count = 2; minimum = 0x800; }
  else if (c >= 0xf0 && c <= 0xf4) { c &= 0x07; count = 3; minimum = 0x10000; }
  else { *text = (const char *)p; return c < 0x80 ? c : 0xfffd; }
  for (unsigned i = 0; i < count; i++) {
    if (p[i] == 0 || (p[i] & 0xc0) != 0x80) { (*text)++; return 0xfffd; }
    c = (c << 6) | (p[i] & 0x3f);
  }
  if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
    (*text)++; return 0xfffd;
  }
  *text = (const char *)(p + count);
  return c;
}

static const uint16_t *
text_kana_glyph(uint32_t codepoint)
{
  size_t low = 0, high = GAME_KANA_GLYPH_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (game_kana_glyphs[middle].codepoint < codepoint) low = middle + 1;
    else high = middle;
  }
  if (low < GAME_KANA_GLYPH_COUNT && game_kana_glyphs[low].codepoint == codepoint)
    return game_kana_glyphs[low].rows;
  low = 0;
  high = GAME_KANJI_GLYPH_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (game_kanji_glyphs[middle].codepoint < codepoint) low = middle + 1;
    else high = middle;
  }
  return low < GAME_KANJI_GLYPH_COUNT && game_kanji_glyphs[low].codepoint == codepoint
    ? game_kanji_glyphs[low].rows : NULL;
}

static void
draw_text(int x, int y, const char *text, uint16_t color)
{
  int64_t cursor_x = x;
  uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
  const char *scan = text;
  int width = 0;
  if (text == NULL || *text == '\0') return;
  while (*scan) {
    (void)text_next_utf8(&scan);
    width = width > INT_MAX - 12 ? INT_MAX : width + 12;
  }
  display_dirty_add(x, y, width, 12);
  while (*text) {
    uint32_t character = text_next_utf8(&text);
    const uint16_t *kana = text_kana_glyph(character);
    const uint8_t *ascii;
    ascii = game_ascii_glyphs[character >= 32 && character <= 126 ? character - 32 : '?' - 32];
    if (cursor_x < GAME_DISPLAY_WIDTH && cursor_x + 12 > 0) {
      for (int row = 0; row < 12; row++) {
        int64_t py = (int64_t)y + row;
        if (py < 0 || py >= GAME_DISPLAY_HEIGHT) continue;
        for (int col = 0; col < (kana ? 12 : 6); col++) {
          int64_t px = cursor_x + col + (kana ? 0 : 3);
          bool set = kana ? ((kana[row] >> (11 - col)) & 1) :
            ((ascii[row] >> (5 - col)) & 1);
          if (set && px >= 0 && px < GAME_DISPLAY_WIDTH) {
            uint8_t *dest = framebuffer + ((size_t)py * GAME_DISPLAY_WIDTH + (size_t)px) * 2u;
            dest[0] = hi; dest[1] = lo;
          }
        }
      }
    }
    cursor_x += 12;
  }
}

void
game_display_text(int x, int y, const char *text, uint16_t color)
{
#if GAME_ENABLE_PROFILING
  uint32_t profile_started = profile_start();
#endif
  draw_text(x, y, text, color);
#if GAME_ENABLE_PROFILING
  profile_add(&profile_text_us, profile_started);
#endif
}

void
game_display_error(const char *title, const char *detail)
{
  const char *heading = "SYSTEM ERROR";
  const char *action = "CHECK USB LOG";
  const char *code = "E01";
  (void)detail; /* Full diagnostics remain on USB serial. */
  if (!tft_ready) return;
  if (strcmp(title, "CARTRIDGE") == 0) {
    heading = "CARTRIDGE ERROR";
    action = "CHECK SD CARD";
    code = "E02";
  } else if (strcmp(title, "VM") == 0) {
    heading = "SYSTEM ERROR";
    code = "E03";
  } else if (strcmp(title, "GAME") == 0) {
    heading = "GAME ERROR";
    action = "CHECK USB LOG";
    code = "E04";
  }
  game_display_clear(GAME_SYSTEM_DARK);
  game_display_fill_rect(12, 12, 296, 216, GAME_SYSTEM_RUBY, 255);
  game_display_fill_rect(20, 20, 280, 200, GAME_SYSTEM_DARK, 255);
  game_display_fill_rect(148, 36, 24, 24, GAME_SYSTEM_ACCENT, 255);
  game_display_fill_rect(158, 40, 4, 10, GAME_SYSTEM_DARK, 255);
  game_display_fill_rect(158, 53, 4, 3, GAME_SYSTEM_DARK, 255);
  draw_text(88, 76, "GAME STOPPED", GAME_SYSTEM_LIGHT);
  draw_text((320 - (int)strlen(heading) * 12) / 2, 110, heading, 0xffff);
  draw_text((320 - (int)strlen(action) * 12) / 2, 138, action, GAME_SYSTEM_LIGHT);
  draw_text(64, 170, "RESTART TO RETRY", GAME_SYSTEM_LIGHT);
  draw_text(142, 196, code, GAME_SYSTEM_ACCENT);
  game_display_present();
}

/* The boot title uses the built-in ASCII glyphs at twice their native size. */
static void
draw_boot_title(void)
{
  const char *title = "PICORUBY GAMING";
  const int scale = 2, advance = 16;
  int x = (GAME_DISPLAY_WIDTH - (int)strlen(title) * advance) / 2;
  int y = (GAME_DISPLAY_HEIGHT - 12 * scale) / 2;
  for (const char *p = title; *p; p++, x += advance) {
    const uint8_t *glyph = game_ascii_glyphs[(unsigned char)*p - 32];
    for (int row = 0; row < 12; row++) {
      for (int col = 0; col < 6; col++) {
        if ((glyph[row] >> (5 - col)) & 1)
          game_display_fill_rect(x + col * scale, y + row * scale,
                                 scale, scale, GAME_SYSTEM_LIGHT, 255);
      }
    }
  }
}

bool
game_display_boot(void)
{
  if (!tft_ready) return false;
  game_display_clear(GAME_SYSTEM_RUBY);
  game_display_fill_rect(12, 12, 296, 216, GAME_SYSTEM_DARK, 255);
  game_display_fill_rect(20, 20, 280, 200, GAME_SYSTEM_RUBY, 255);
  draw_boot_title();
  if (!game_display_present()) return false;
  /* Leave the splash on the panel but start both game buffers black. A game
   * may draw only part of its first frame without explicitly clearing it. */
#if GAME_TFT_CORE1
  if (tft_core1_started) {
    tft_core1_wait_for_slot(0);
    tft_core1_wait_for_slot(1);
  }
  memset(framebuffers[0], 0, GAME_FRAMEBUFFER_BYTES);
  memset(framebuffers[1], 0, GAME_FRAMEBUFFER_BYTES);
#else
  game_display_clear(0);
#endif
  display_dirty_add(0, 0, GAME_DISPLAY_WIDTH, GAME_DISPLAY_HEIGHT);
  return true;
}

void
game_frame_tick(uint32_t fps)
{
#if GAME_AUDIO_I2S && GAME_AUDIO_I2S_CHAIN_DMA
  audio_i2s_watchdog();
#endif
  game_assets_sweep();
  uint32_t period = 1000000u / fps;
  absolute_time_t now = get_absolute_time();

#if GAME_ENABLE_PROFILING
  profile_report();
#endif

  if (!frame_deadline_set) {
    frame_deadline = delayed_by_us(now, period);
    frame_deadline_set = true;
    game_input_begin_frame();
    game_time_begin_frame();
#if GAME_ENABLE_PROFILING
    profile_begin_frame();
#endif
    return;
  }

  while (absolute_time_diff_us(now, frame_deadline) > 0) {
    tight_loop_contents();
    now = get_absolute_time();
  }
  frame_deadline = delayed_by_us(frame_deadline, period);
  if (absolute_time_diff_us(now, frame_deadline) < -(int64_t)period * 4) {
    frame_deadline = delayed_by_us(now, period);
  }
  game_input_begin_frame();
  game_time_begin_frame();
#if GAME_ENABLE_PROFILING
  profile_begin_frame();
#endif
}
