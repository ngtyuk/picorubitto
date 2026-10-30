#ifndef PICO_RUBY_GAMING_GAME_PLATFORM_H
#define PICO_RUBY_GAMING_GAME_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GAME_DISPLAY_WIDTH 320u
#define GAME_DISPLAY_HEIGHT 240u
#define GAME_FRAMEBUFFER_BYTES (GAME_DISPLAY_WIDTH * GAME_DISPLAY_HEIGHT * 2u)
#define GAME_ROM_FILENAME "game.mrb"
#define GAME_API_VERSION 16u
#define GAME_PSRAM_TOTAL_BYTES (8u * 1024u * 1024u)
#define GAME_PSRAM_VM_HEAP_SIZE (4u * 1024u * 1024u)
#define GAME_PSRAM_BASE_ADDRESS 0x11000000u
#define GAME_SAVE_MAX_BYTES (32u * 1024u)
#define GAME_SAVE_SLOT_MAX 24u

/* GP15 is the PWM audio input for the external amplifier. */
#define GAME_AUDIO_GPIO 15u

#define GAME_CAPABILITY_DISPLAY (1u << 0)
#define GAME_CAPABILITY_INPUT (1u << 1)
#define GAME_CAPABILITY_ASSETS (1u << 2)
#define GAME_CAPABILITY_SPRITES (1u << 3)
#define GAME_CAPABILITY_AUDIO (1u << 4)
#define GAME_CAPABILITY_SAVE (1u << 5)
#define GAME_CAPABILITY_COLLISION (1u << 6)
#define GAME_CAPABILITY_TEXTURES (1u << 7)
#define GAME_CAPABILITY_FONTS (1u << 8)
#define GAME_CAPABILITY_SHAPES (1u << 9)
#define GAME_CAPABILITY_AUDIO_MIXING (1u << 10)
#define GAME_CAPABILITY_THREE_D (1u << 11)
#define GAME_CAPABILITY_THREE_D_MODELS (1u << 12)
#define GAME_CAPABILITY_AUDIO_DIAL (1u << 13)
#define GAME_3D_INDEXED_MAX_VERTICES 256u

enum game_button {
  GAME_BUTTON_UP = 1u << 0,
  GAME_BUTTON_DOWN = 1u << 1,
  GAME_BUTTON_LEFT = 1u << 2,
  GAME_BUTTON_RIGHT = 1u << 3,
  GAME_BUTTON_A = 1u << 4,
  GAME_BUTTON_B = 1u << 5,
  GAME_BUTTON_L = 1u << 6,
  GAME_BUTTON_R = 1u << 7,
  GAME_BUTTON_START = 1u << 8,
  GAME_BUTTON_SELECT = 1u << 9,
};

bool game_platform_boot(void);
const char *game_platform_boot_error(void);
void *game_vm_heap(void);
size_t game_vm_heap_size(void);

uint32_t game_input_state(void);
bool game_input_pressed(uint32_t button);
bool game_input_just_pressed(uint32_t button);
bool game_input_just_released(uint32_t button);
void game_input_begin_frame(void);

void game_display_clear(uint16_t color);
bool game_display_set_background(const uint8_t *data, size_t data_len);
const uint8_t *game_display_background_source(void);
void game_display_clear_background(void);
void game_display_begin_frame(void);
bool game_display_fill_rect(int x, int y, int width, int height, uint16_t color,
                            uint8_t alpha);
bool game_display_draw_circle(int x, int y, int radius, uint16_t color,
                              uint8_t alpha);
bool game_display_draw_line(int x1, int y1, int x2, int y2, int thickness,
                            uint16_t color, uint8_t alpha);
bool game_display_blit_rgb565(int x, int y, int width, int height,
                              const uint8_t *data, size_t data_len);
bool game_display_blit_rgb565_region(int x, int y, int width, int height,
                                     const uint8_t *data, size_t data_len,
                                     int source_width, int source_x,
                                     int source_y);
void game_display_text(int x, int y, const char *text, uint16_t color);
bool game_display_present(void);
bool game_display_boot(void);
void game_display_error(const char *title, const char *detail);

bool game_3d_supported(void);
bool game_3d_begin_frame(void);
bool game_3d_set_viewport(int x, int y, int width, int height);
bool game_3d_set_resolution(int width, int height);
bool game_3d_set_camera(float x, float y, float z, float yaw, float pitch,
                        float fov_degrees, float near_clip, float far_clip);
bool game_3d_draw_triangle(float x1, float y1, float z1,
                           float x2, float y2, float z2,
                           float x3, float y3, float z3,
                           uint16_t color);
bool game_3d_draw_indexed_mesh(const float *vertices, size_t vertex_count,
                               const uint16_t *indices, size_t triangle_count,
                               const uint16_t *colors);
bool game_3d_draw_wireframe(const float *vertices, size_t vertex_count,
                            const uint16_t *edges, size_t edge_count,
                            uint16_t color, bool depth_test);
/* Internal Model backend entrypoint; not a cartridge API. */
bool game_3d_draw_model_geometry(const float *vertices, size_t vertex_count,
                            const uint16_t *indices, size_t triangle_count,
                            const uint16_t *colors, const uint16_t *edges,
                            size_t edge_count, uint16_t color, bool depth_test);

typedef struct game_sprite game_sprite_t;
void game_sprite_release(game_sprite_t *sprite);
game_sprite_t *game_sprite_load(const char *path, uint16_t frame_width,
                                uint16_t frame_height, uint16_t frame_count,
                                uint16_t key_color);
bool game_display_draw_sprite(const game_sprite_t *sprite, int x, int y,
                              uint16_t frame);
uint16_t game_sprite_frame_width(const game_sprite_t *sprite);
uint16_t game_sprite_frame_height(const game_sprite_t *sprite);
uint16_t game_sprite_frame_count(const game_sprite_t *sprite);

typedef struct game_texture game_texture_t;
void game_texture_release(game_texture_t *texture);
game_texture_t *game_texture_load(const char *path, uint16_t width,
                                  uint16_t height, uint16_t key_color);
bool game_display_draw_texture(const game_texture_t *texture, int x, int y,
                               int source_x, int source_y, int source_width,
                               int source_height, int destination_width,
                               int destination_height);
uint16_t game_texture_width(const game_texture_t *texture);
uint16_t game_texture_height(const game_texture_t *texture);
uint32_t game_texture_checksum(const game_texture_t *texture);

typedef struct game_font game_font_t;
void game_font_release(game_font_t *font);
game_font_t *game_font_load(const char *path);
int game_font_measure(const game_font_t *font, const char *text,
                      uint16_t pixel_size, int spacing);
bool game_display_draw_font(const game_font_t *font, int x, int y,
                            const char *text, uint16_t pixel_size, int spacing,
                            uint16_t color);

typedef struct game_audio_sound game_audio_sound_t;
void game_audio_sound_release(game_audio_sound_t *sound);
game_audio_sound_t *game_audio_sound_load(const char *path);
/* Bounded reuse for Audio.play; Sound.load keeps independent GC-owned handles. */
game_audio_sound_t *game_audio_sound_load_cached(const char *path);
bool game_audio_sound_play(game_audio_sound_t *sound, uint8_t volume);
bool game_audio_sound_play_bgm(game_audio_sound_t *sound, uint8_t volume, bool loop);
void game_audio_bgm_stop(void);
void game_audio_sound_stop(game_audio_sound_t *sound);
bool game_audio_play(const char *path);
void game_audio_stop(void);
bool game_audio_busy(void);
bool game_audio_dial_supported(void);
uint16_t game_audio_dial_value(void);
uint8_t game_audio_master_volume(void);
void game_assets_collect(void);
void game_assets_sweep(void);
size_t game_assets_resident_bytes(void);

uint32_t game_time_millis(void);
void game_time_reset(void);
void game_time_begin_frame(void);
uint32_t game_time_delta_ms(void);

bool game_storage_load_rom(uint8_t **data, size_t *size);
bool game_storage_file_size(const char *path, size_t *size);
bool game_storage_read_file(const char *path, uint8_t *data, size_t capacity,
                            size_t *size);
bool game_storage_save_supported(void);
bool game_storage_save_exists(const char *slot);
bool game_storage_save_file_size(const char *slot, size_t *size);
bool game_storage_save_read(const char *slot, uint8_t *data, size_t capacity,
                            size_t *size);
bool game_storage_save_write(const char *slot, const uint8_t *data, size_t size);
bool game_storage_save_delete(const char *slot);

void game_frame_tick(uint32_t fps);

#endif
