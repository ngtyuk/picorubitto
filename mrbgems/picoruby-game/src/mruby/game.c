#include <limits.h>
#include <math.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/presym.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "game_platform.h"

void game_model_init(mrb_state *mrb, struct RClass *three_d);
size_t game_model_resident_bytes(void);

static struct RClass *game_input_module;
static struct RClass *game_display_module;
static struct RClass *game_3d_module;
static struct RClass *game_assets_module;
static struct RClass *game_sprite_class;
static struct RClass *game_texture_class;
static struct RClass *game_font_class;
static struct RClass *game_audio_module;
static struct RClass *game_audio_sound_class;
static struct RClass *game_time_module;
static struct RClass *game_runtime_module;
static struct RClass *game_save_module;
static struct RClass *game_collision_module;

static void
game_sprite_free(mrb_state *mrb, void *ptr)
{
  (void)mrb;
  game_sprite_release(ptr);
}

static const struct mrb_data_type game_sprite_type = {
  "Game::Sprite", game_sprite_free,
};

static void
game_texture_free(mrb_state *mrb, void *ptr)
{
  (void)mrb;
  game_texture_release(ptr);
}

static const struct mrb_data_type game_texture_type = {
  "Game::Texture", game_texture_free,
};

static void
game_font_free(mrb_state *mrb, void *ptr)
{
  (void)mrb;
  game_font_release(ptr);
}

static const struct mrb_data_type game_font_type = {
  "Game::Font", game_font_free,
};

static void
game_audio_sound_free(mrb_state *mrb, void *ptr)
{
  (void)mrb;
  game_audio_sound_release(ptr);
}

static const struct mrb_data_type game_audio_sound_type = {
  "Game::Audio::Sound", game_audio_sound_free,
};

static void
collect_assets(mrb_state *mrb)
{
  mrb_full_gc(mrb);
  game_assets_collect();
}

static mrb_value
assets_collect(mrb_state *mrb, mrb_value self)
{
  (void)self;
  collect_assets(mrb);
  return mrb_nil_value();
}

static mrb_value
assets_resident_bytes(mrb_state *mrb, mrb_value self)
{
  (void)self;
  return mrb_int_value(mrb, (mrb_int)(game_assets_resident_bytes() + game_model_resident_bytes()));
}

static mrb_value
game_init(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  game_time_reset();
  return mrb_true_value();
}

static mrb_value
input_state(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value((mrb_int)game_input_state());
}

static uint32_t
input_button_mask(mrb_state *mrb)
{
  mrb_int button;
  uint32_t mask;
  const uint32_t supported = GAME_BUTTON_UP | GAME_BUTTON_DOWN |
                             GAME_BUTTON_LEFT | GAME_BUTTON_RIGHT |
                             GAME_BUTTON_A | GAME_BUTTON_B | GAME_BUTTON_L |
                             GAME_BUTTON_R | GAME_BUTTON_START |
                             GAME_BUTTON_SELECT;

  mrb_get_args(mrb, "i", &button);
  mask = (uint32_t)button;
  if (button <= 0 || mask == 0 || (mask & ~supported) != 0 ||
      (mask & (mask - 1u)) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "button must be one Game::Input button constant");
  }
  return mask;
}

static mrb_value
input_pressed(mrb_state *mrb, mrb_value self)
{
  (void)self;
  return mrb_bool_value(game_input_pressed(input_button_mask(mrb)));
}

static mrb_value
input_just_pressed(mrb_state *mrb, mrb_value self)
{
  (void)self;
  return mrb_bool_value(game_input_just_pressed(input_button_mask(mrb)));
}

static mrb_value
input_just_released(mrb_state *mrb, mrb_value self)
{
  (void)self;
  return mrb_bool_value(game_input_just_released(input_button_mask(mrb)));
}

static mrb_value
display_width(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value(GAME_DISPLAY_WIDTH);
}

static mrb_value
display_height(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value(GAME_DISPLAY_HEIGHT);
}

static mrb_value
display_clear(mrb_state *mrb, mrb_value self)
{
  mrb_int color;
  (void)self;
  mrb_get_args(mrb, "i", &color);
  if (color < 0 || color > 0xffff) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "RGB565 color must be 0..65535");
  }
  game_display_clear((uint16_t)color);
  return mrb_nil_value();
}

static mrb_value
display_set_background(mrb_state *mrb, mrb_value self)
{
  mrb_value data;
  (void)self;
  mrb_get_args(mrb, "o", &data);
  if (mrb_nil_p(data)) {
    game_display_clear_background();
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@background"), data);
    return data;
  }
  if (!mrb_string_p(data)) {
    mrb_raise(mrb, E_TYPE_ERROR, "background data must be a String");
  }
  if (!game_display_set_background((const uint8_t *)RSTRING_PTR(data),
                                   (size_t)RSTRING_LEN(data))) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "background must be a full-screen RGB565 bitmap");
  }
  mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@background"), data);
  return mrb_nil_value();
}

static mrb_value
display_begin_frame(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  game_display_begin_frame();
  return mrb_nil_value();
}

static mrb_value
display_fill_rect(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, width, height, color, alpha = 255;
  (void)self;
  mrb_get_args(mrb, "iiiii|i", &x, &y, &width, &height, &color, &alpha);
  if (color < 0 || color > 0xffff || alpha < 0 || alpha > 255) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "RGB565 color must be 0..65535");
  }
  if (!game_display_fill_rect((int)x, (int)y, (int)width, (int)height,
                              (uint16_t)color, (uint8_t)alpha)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "rectangle width and height must be positive");
  }
  return mrb_nil_value();
}

static mrb_value
display_draw_circle(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, radius, color, alpha = 255;
  (void)self;
  mrb_get_args(mrb, "iiii|i", &x, &y, &radius, &color, &alpha);
  if (radius < 0 || radius > 320 || color < 0 || color > 0xffff ||
      alpha < 0 || alpha > 255) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid circle or alpha value");
  }
  if (!game_display_draw_circle((int)x, (int)y, (int)radius,
                                (uint16_t)color, (uint8_t)alpha)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "circle drawing failed");
  }
  return mrb_nil_value();
}

static mrb_value
display_draw_line(mrb_state *mrb, mrb_value self)
{
  mrb_int x1, y1, x2, y2, thickness, color, alpha = 255;
  (void)self;
  mrb_get_args(mrb, "iiiiii|i", &x1, &y1, &x2, &y2, &thickness, &color,
               &alpha);
  if (thickness <= 0 || thickness > 320 || color < 0 || color > 0xffff ||
      alpha < 0 || alpha > 255) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid line or alpha value");
  }
  if (!game_display_draw_line((int)x1, (int)y1, (int)x2, (int)y2,
                              (int)thickness, (uint16_t)color,
                              (uint8_t)alpha)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "line drawing failed");
  }
  return mrb_nil_value();
}

static mrb_value
display_blit_rgb565(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, width, height;
  mrb_value data;
  (void)self;
  mrb_get_args(mrb, "iiiio", &x, &y, &width, &height, &data);
  if (!mrb_string_p(data)) {
    mrb_raise(mrb, E_TYPE_ERROR, "RGB565 data must be a String");
  }
  if (!game_display_blit_rgb565((int)x, (int)y, (int)width, (int)height,
                                (const uint8_t *)RSTRING_PTR(data),
                                (size_t)RSTRING_LEN(data))) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid RGB565 bitmap or destination");
  }
  if (game_display_background_source() == (const uint8_t *)RSTRING_PTR(data)) {
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@background"), data);
  }
  return mrb_nil_value();
}

static mrb_value
display_blit_rgb565_region(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, width, height;
  mrb_int source_width, source_x, source_y;
  mrb_value data;
  (void)self;
  mrb_get_args(mrb, "iiiioiii", &x, &y, &width, &height, &data,
               &source_width, &source_x, &source_y);
  if (!mrb_string_p(data)) {
    mrb_raise(mrb, E_TYPE_ERROR, "RGB565 data must be a String");
  }
  if (!game_display_blit_rgb565_region(
        (int)x, (int)y, (int)width, (int)height,
        (const uint8_t *)RSTRING_PTR(data), (size_t)RSTRING_LEN(data),
        (int)source_width, (int)source_x, (int)source_y)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid RGB565 source or destination");
  }
  return mrb_nil_value();
}

static mrb_value
display_present(mrb_state *mrb, mrb_value self)
{
  (void)self;
  if (!game_display_present()) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "TFT transfer failed");
  }
  return mrb_nil_value();
}

static bool
game_3d_number(mrb_value value, float *result)
{
  if (mrb_float_p(value)) {
    *result = (float)mrb_float(value);
    return isfinite(*result);
  }
  if (mrb_integer_p(value)) {
    *result = (float)mrb_integer(value);
    return isfinite(*result);
  }
  return false;
}

static mrb_value
three_d_begin_frame(mrb_state *mrb, mrb_value self)
{
  (void)self;
  if (!game_3d_begin_frame()) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "3D renderer is unavailable");
  }
  return mrb_nil_value();
}

static mrb_value
three_d_set_viewport(mrb_state *mrb, mrb_value self)
{
  mrb_int x;
  mrb_int y;
  mrb_int width;
  mrb_int height;

  (void)self;
  mrb_get_args(mrb, "iiii", &x, &y, &width, &height);
  if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
      width > (mrb_int)GAME_DISPLAY_WIDTH ||
      height > (mrb_int)GAME_DISPLAY_HEIGHT ||
      x > (mrb_int)GAME_DISPLAY_WIDTH - width ||
      y > (mrb_int)GAME_DISPLAY_HEIGHT - height) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D viewport");
  }
  if (!game_3d_set_viewport((int)x, (int)y, (int)width, (int)height)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D viewport");
  }
  return mrb_nil_value();
}

static mrb_value
three_d_set_resolution(mrb_state *mrb, mrb_value self)
{
  mrb_int width;
  mrb_int height;

  (void)self;
  mrb_get_args(mrb, "ii", &width, &height);
  if (width <= 0 || height <= 0 || width > (mrb_int)GAME_DISPLAY_WIDTH ||
      height > (mrb_int)GAME_DISPLAY_HEIGHT) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D internal resolution");
  }
  if (!game_3d_set_resolution((int)width, (int)height)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D internal resolution");
  }
  return mrb_nil_value();
}

static mrb_value
three_d_set_camera(mrb_state *mrb, mrb_value self)
{
  mrb_value *arguments;
  mrb_int argument_count;
  float values[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                     70.0f, 0.1f, 100.0f};

  (void)self;
  mrb_get_args(mrb, "*", &arguments, &argument_count);
  if (argument_count < 5 || argument_count > 8) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "3D camera expects 5 to 8 numeric arguments");
  }
  for (mrb_int index = 0; index < argument_count; index++) {
    if (!game_3d_number(arguments[index], &values[index])) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D camera values must be numbers");
    }
  }
  if (!game_3d_set_camera(values[0], values[1], values[2], values[3],
                          values[4], values[5], values[6], values[7])) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D camera parameters");
  }
  return mrb_nil_value();
}

static mrb_value
three_d_draw_triangle(mrb_state *mrb, mrb_value self)
{
  mrb_value *arguments;
  mrb_int argument_count;
  float values[9];
  mrb_int color;

  (void)self;
  mrb_get_args(mrb, "*", &arguments, &argument_count);
  if (argument_count != 10) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "3D triangle expects 9 coordinates and a color");
  }
  for (mrb_int index = 0; index < 9; index++) {
    if (!game_3d_number(arguments[index], &values[index])) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D coordinates must be numbers");
    }
  }
  if (!mrb_integer_p(arguments[9])) {
    mrb_raise(mrb, E_TYPE_ERROR, "3D color must be an integer");
  }
  color = mrb_integer(arguments[9]);
  if (color < 0 || color > 0xffff ||
      !game_3d_draw_triangle(values[0], values[1], values[2],
                             values[3], values[4], values[5],
                             values[6], values[7], values[8],
                             (uint16_t)color)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "invalid 3D triangle or frame not begun");
  }
  return mrb_nil_value();
}

static mrb_value
three_d_draw_triangles(mrb_state *mrb, mrb_value self)
{
  mrb_value triangles;
  mrb_int length;

  (void)self;
  mrb_get_args(mrb, "A", &triangles);
  length = RARRAY_LEN(triangles);
  if ((length % 10) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "3D triangle batch must contain 10-value records");
  }
  for (mrb_int offset = 0; offset < length; offset += 10) {
    float values[9];
    mrb_int color;
    for (mrb_int index = 0; index < 9; index++) {
      mrb_value value = mrb_ary_ref(mrb, triangles, offset + index);
      if (!game_3d_number(value, &values[index])) {
        mrb_raise(mrb, E_TYPE_ERROR, "3D coordinates must be numbers");
      }
    }
    mrb_value color_value = mrb_ary_ref(mrb, triangles, offset + 9);
    if (!mrb_integer_p(color_value)) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D color must be an integer");
    }
    color = mrb_integer(color_value);
    if (color < 0 || color > 0xffff ||
        !game_3d_draw_triangle(values[0], values[1], values[2],
                               values[3], values[4], values[5],
                               values[6], values[7], values[8],
                               (uint16_t)color)) {
      mrb_raise(mrb, E_ARGUMENT_ERROR,
                "invalid 3D triangle or frame not begun");
    }
  }
  return mrb_nil_value();
}

static mrb_value
three_d_draw_indexed_mesh(mrb_state *mrb, mrb_value self)
{
  mrb_value vertices;
  mrb_value indices;
  mrb_value colors;
  mrb_int vertex_value_count;
  mrb_int index_value_count;
  mrb_int color_count;
  size_t vertex_count;
  size_t index_count;
  size_t triangle_count;
  size_t vertex_bytes;
  size_t index_bytes;
  size_t color_bytes;
  size_t total_bytes;
  uint8_t *storage;
  float *vertex_values;
  uint16_t *index_values;
  uint16_t *color_values;

  (void)self;
  mrb_get_args(mrb, "AAA", &vertices, &indices, &colors);
  vertex_value_count = RARRAY_LEN(vertices);
  index_value_count = RARRAY_LEN(indices);
  color_count = RARRAY_LEN(colors);
  if ((vertex_value_count % 3) != 0 || (index_value_count % 3) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "3D indexed mesh arrays must contain XYZ and index triples");
  }
  vertex_count = (size_t)(vertex_value_count / 3);
  index_count = (size_t)index_value_count;
  triangle_count = index_count / 3u;
  if (vertex_count > GAME_3D_INDEXED_MAX_VERTICES ||
      (size_t)color_count != triangle_count ||
      (triangle_count > 0 && vertex_count == 0)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D indexed mesh sizes");
  }
  for (mrb_int index = 0; index < vertex_value_count; index++) {
    float value;
    if (!game_3d_number(mrb_ary_ref(mrb, vertices, index), &value)) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D mesh vertices must be numbers");
    }
  }
  for (mrb_int index = 0; index < index_value_count; index++) {
    mrb_value value = mrb_ary_ref(mrb, indices, index);
    mrb_int vertex_index;
    if (!mrb_integer_p(value)) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D mesh indices must be integers");
    }
    vertex_index = mrb_integer(value);
    if (vertex_index < 0 || (size_t)vertex_index >= vertex_count ||
        vertex_index > UINT16_MAX) {
      mrb_raise(mrb, E_ARGUMENT_ERROR, "3D mesh index is out of range");
    }
  }
  for (mrb_int index = 0; index < color_count; index++) {
    mrb_value value = mrb_ary_ref(mrb, colors, index);
    mrb_int color;
    if (!mrb_integer_p(value)) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D mesh colors must be integers");
    }
    color = mrb_integer(value);
    if (color < 0 || color > 0xffff) {
      mrb_raise(mrb, E_ARGUMENT_ERROR, "3D mesh color must be RGB565");
    }
  }
  if (triangle_count == 0) return mrb_nil_value();
  if (vertex_count > SIZE_MAX / (3u * sizeof(float)) ||
      index_count > SIZE_MAX / sizeof(uint16_t) ||
      triangle_count > SIZE_MAX / sizeof(uint16_t)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "3D indexed mesh is too large");
  }
  vertex_bytes = vertex_count * 3u * sizeof(float);
  index_bytes = index_count * sizeof(uint16_t);
  color_bytes = triangle_count * sizeof(uint16_t);
  if (vertex_bytes > SIZE_MAX - index_bytes ||
      vertex_bytes + index_bytes > SIZE_MAX - color_bytes) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "3D indexed mesh is too large");
  }
  total_bytes = vertex_bytes + index_bytes + color_bytes;
  storage = (uint8_t *)mrb_malloc(mrb, total_bytes);
  vertex_values = (float *)storage;
  index_values = (uint16_t *)(storage + vertex_bytes);
  color_values = (uint16_t *)(storage + vertex_bytes + index_bytes);
  for (mrb_int index = 0; index < vertex_value_count; index++) {
    game_3d_number(mrb_ary_ref(mrb, vertices, index), &vertex_values[index]);
  }
  for (mrb_int index = 0; index < index_value_count; index++) {
    index_values[index] = (uint16_t)mrb_integer(
      mrb_ary_ref(mrb, indices, index));
  }
  for (mrb_int index = 0; index < color_count; index++) {
    color_values[index] = (uint16_t)mrb_integer(
      mrb_ary_ref(mrb, colors, index));
  }
  if (!game_3d_draw_indexed_mesh(vertex_values, vertex_count, index_values,
                                 triangle_count, color_values)) {
    mrb_free(mrb, storage);
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "invalid 3D indexed mesh or frame not begun");
  }
  mrb_free(mrb, storage);
  return mrb_nil_value();
}

static mrb_value
three_d_draw_wireframe(mrb_state *mrb, mrb_value self)
{
  mrb_value vertices;
  mrb_value edges;
  mrb_value color_value;
  mrb_value depth_test_value = mrb_true_value();
  mrb_int vertex_value_count;
  mrb_int edge_value_count;
  mrb_int color;
  mrb_bool depth_test = TRUE;
  size_t vertex_count;
  size_t edge_count;
  size_t vertex_bytes;
  size_t edge_bytes;
  size_t total_bytes;
  uint8_t *storage;
  float *vertex_values;
  uint16_t *edge_values;

  (void)self;
  mrb_get_args(mrb, "AAo|o", &vertices, &edges, &color_value,
               &depth_test_value);
  if (mrb_true_p(depth_test_value)) {
    depth_test = TRUE;
  } else if (mrb_false_p(depth_test_value)) {
    depth_test = FALSE;
  } else {
    mrb_raise(mrb, E_TYPE_ERROR,
              "wireframe depth_test must be true or false");
  }
  if (!mrb_integer_p(color_value)) {
    mrb_raise(mrb, E_TYPE_ERROR, "wireframe color must be an integer");
  }
  color = mrb_integer(color_value);
  if (color < 0 || color > 0xffff) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "wireframe color must be an RGB565 integer");
  }

  vertex_value_count = RARRAY_LEN(vertices);
  edge_value_count = RARRAY_LEN(edges);
  if ((vertex_value_count % 3) != 0 || (edge_value_count % 2) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "3D wireframe arrays must contain XYZ and edge pairs");
  }
  vertex_count = (size_t)(vertex_value_count / 3);
  edge_count = (size_t)(edge_value_count / 2);
  if (vertex_count > GAME_3D_INDEXED_MAX_VERTICES ||
      (edge_count > 0 && vertex_count == 0)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid 3D wireframe sizes");
  }
  for (mrb_int index = 0; index < vertex_value_count; index++) {
    float value;
    if (!game_3d_number(mrb_ary_ref(mrb, vertices, index), &value)) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D wireframe vertices must be numbers");
    }
  }
  for (mrb_int index = 0; index < edge_value_count; index++) {
    mrb_value value = mrb_ary_ref(mrb, edges, index);
    mrb_int vertex_index;
    if (!mrb_integer_p(value)) {
      mrb_raise(mrb, E_TYPE_ERROR, "3D wireframe edges must be integers");
    }
    vertex_index = mrb_integer(value);
    if (vertex_index < 0 || (size_t)vertex_index >= vertex_count ||
        vertex_index > UINT16_MAX) {
      mrb_raise(mrb, E_ARGUMENT_ERROR,
                "3D wireframe edge index is out of range");
    }
  }
  if (edge_count == 0) return mrb_nil_value();
  if (vertex_count > SIZE_MAX / (3u * sizeof(float)) ||
      edge_count > SIZE_MAX / (2u * sizeof(uint16_t))) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "3D wireframe is too large");
  }
  vertex_bytes = vertex_count * 3u * sizeof(float);
  edge_bytes = edge_count * 2u * sizeof(uint16_t);
  if (vertex_bytes > SIZE_MAX - edge_bytes) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "3D wireframe is too large");
  }
  total_bytes = vertex_bytes + edge_bytes;
  storage = (uint8_t *)mrb_malloc(mrb, total_bytes);
  vertex_values = (float *)storage;
  edge_values = (uint16_t *)(storage + vertex_bytes);
  for (mrb_int index = 0; index < vertex_value_count; index++) {
    game_3d_number(mrb_ary_ref(mrb, vertices, index), &vertex_values[index]);
  }
  for (mrb_int index = 0; index < edge_value_count; index++) {
    edge_values[index] = (uint16_t)mrb_integer(
      mrb_ary_ref(mrb, edges, index));
  }
  if (!game_3d_draw_wireframe(vertex_values, vertex_count, edge_values,
                              edge_count, (uint16_t)color, depth_test)) {
    mrb_free(mrb, storage);
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "invalid 3D wireframe or frame not begun");
  }
  mrb_free(mrb, storage);
  return mrb_nil_value();
}

static mrb_value
display_text(mrb_state *mrb, mrb_value self)
{
  mrb_int x;
  mrb_int y;
  mrb_int color = 0xffff;
  const char *text;
  (void)self;
  mrb_get_args(mrb, "iiz|i", &x, &y, &text, &color);
  if (color < 0 || color > 0xffff) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "RGB565 color must be 0..65535");
  }
  game_display_text((int)x, (int)y, text, (uint16_t)color);
  return mrb_nil_value();
}

static mrb_value
sprite_load(mrb_state *mrb, mrb_value klass)
{
  const char *path;
  mrb_int frame_width;
  mrb_int frame_height;
  mrb_int frame_count;
  mrb_int key_color = 0xf81f;
  game_sprite_t *sprite;

  mrb_get_args(mrb, "ziii|i", &path, &frame_width, &frame_height,
               &frame_count, &key_color);
  if (frame_width <= 0 || frame_width > UINT16_MAX || frame_height <= 0 ||
      frame_height > UINT16_MAX || frame_count <= 0 ||
      frame_count > UINT16_MAX || key_color < 0 || key_color > 0xffff) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid sprite dimensions or key color");
  }
  mrb_value object = mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), NULL, &game_sprite_type));
  sprite = game_sprite_load(path, (uint16_t)frame_width, (uint16_t)frame_height, (uint16_t)frame_count, (uint16_t)key_color);
  if (sprite == NULL) { collect_assets(mrb); sprite = game_sprite_load(path, (uint16_t)frame_width, (uint16_t)frame_height, (uint16_t)frame_count, (uint16_t)key_color); }
  if (sprite == NULL) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "sprite asset load failed");
  }
  DATA_PTR(object) = sprite;
  return object;
}

static mrb_value
sprite_draw(mrb_state *mrb, mrb_value self)
{
  mrb_int x;
  mrb_int y;
  mrb_int frame;
  game_sprite_t *sprite;

  mrb_get_args(mrb, "iii", &x, &y, &frame);
  sprite = (game_sprite_t *)mrb_data_get_ptr(mrb, self, &game_sprite_type);
  if (frame < 0 || frame >= game_sprite_frame_count(sprite) ||
      !game_display_draw_sprite(sprite, (int)x, (int)y, (uint16_t)frame)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid sprite frame or destination");
  }
  return mrb_nil_value();
}

static mrb_value
sprite_draw_batch(mrb_state *mrb, mrb_value self)
{
  mrb_value placements;
  game_sprite_t *sprite;
  mrb_int length;

  mrb_get_args(mrb, "A", &placements);
  length = RARRAY_LEN(placements);
  if ((length % 3) != 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "sprite batch must contain x, y, frame triples");
  }
  sprite = (game_sprite_t *)mrb_data_get_ptr(mrb, self, &game_sprite_type);
  for (mrb_int index = 0; index < length; index += 3) {
    mrb_value x_value = mrb_ary_ref(mrb, placements, index);
    mrb_value y_value = mrb_ary_ref(mrb, placements, index + 1);
    mrb_value frame_value = mrb_ary_ref(mrb, placements, index + 2);
    mrb_int x;
    mrb_int y;
    mrb_int frame;

    if (!mrb_integer_p(x_value) || !mrb_integer_p(y_value) ||
        !mrb_integer_p(frame_value)) {
      mrb_raise(mrb, E_TYPE_ERROR,
                "sprite batch values must be integers");
    }
    x = mrb_integer(x_value);
    y = mrb_integer(y_value);
    frame = mrb_integer(frame_value);
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX ||
        frame < 0 || frame >= game_sprite_frame_count(sprite) ||
        !game_display_draw_sprite(sprite, (int)x, (int)y, (uint16_t)frame)) {
      mrb_raise(mrb, E_ARGUMENT_ERROR,
                "invalid sprite batch frame or destination");
    }
  }
  return mrb_nil_value();
}

static mrb_value
sprite_width(mrb_state *mrb, mrb_value self)
{
  game_sprite_t *sprite;
  (void)mrb;
  sprite = (game_sprite_t *)mrb_data_get_ptr(mrb, self, &game_sprite_type);
  return mrb_fixnum_value(game_sprite_frame_width(sprite));
}

static mrb_value
sprite_height(mrb_state *mrb, mrb_value self)
{
  game_sprite_t *sprite;
  (void)mrb;
  sprite = (game_sprite_t *)mrb_data_get_ptr(mrb, self, &game_sprite_type);
  return mrb_fixnum_value(game_sprite_frame_height(sprite));
}

static mrb_value
sprite_frames(mrb_state *mrb, mrb_value self)
{
  game_sprite_t *sprite;
  (void)mrb;
  sprite = (game_sprite_t *)mrb_data_get_ptr(mrb, self, &game_sprite_type);
  return mrb_fixnum_value(game_sprite_frame_count(sprite));
}

static mrb_value
texture_load(mrb_state *mrb, mrb_value klass)
{
  const char *path;
  mrb_int width;
  mrb_int height;
  mrb_int key_color = 0xf81f;
  game_texture_t *texture;

  mrb_get_args(mrb, "zii|i", &path, &width, &height, &key_color);
  if (width <= 0 || width > UINT16_MAX || height <= 0 ||
      height > UINT16_MAX || key_color < 0 || key_color > 0xffff) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid texture dimensions or key color");
  }
  mrb_value object = mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), NULL, &game_texture_type));
  texture = game_texture_load(path, (uint16_t)width, (uint16_t)height, (uint16_t)key_color);
  if (texture == NULL) { collect_assets(mrb); texture = game_texture_load(path, (uint16_t)width, (uint16_t)height, (uint16_t)key_color); }
  if (texture == NULL) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "texture asset load failed");
  }
  DATA_PTR(object) = texture;
  return object;
}

static mrb_value
texture_draw(mrb_state *mrb, mrb_value self)
{
  mrb_int x;
  mrb_int y;
  mrb_int source_x = 0;
  mrb_int source_y = 0;
  mrb_int source_width;
  mrb_int source_height;
  mrb_int destination_width;
  mrb_int destination_height;
  game_texture_t *texture;

  texture = (game_texture_t *)mrb_data_get_ptr(mrb, self, &game_texture_type);
  source_width = game_texture_width(texture);
  source_height = game_texture_height(texture);
  destination_width = source_width;
  destination_height = source_height;
  mrb_get_args(mrb, "ii|iiiiii", &x, &y, &source_x, &source_y,
               &source_width, &source_height, &destination_width,
               &destination_height);
  if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX ||
      source_x < 0 || source_y < 0 || source_width <= 0 ||
      source_height <= 0 || destination_width <= 0 ||
      destination_height <= 0 || source_x > game_texture_width(texture) ||
      source_y > game_texture_height(texture) ||
      source_width > game_texture_width(texture) - source_x ||
      source_height > game_texture_height(texture) - source_y ||
      destination_width > INT_MAX || destination_height > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid texture source or destination");
  }
  if (!game_display_draw_texture(texture, (int)x, (int)y, (int)source_x,
                                 (int)source_y, (int)source_width,
                                 (int)source_height, (int)destination_width,
                                 (int)destination_height)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid texture source or destination");
  }
  return mrb_nil_value();
}

static mrb_value
texture_width(mrb_state *mrb, mrb_value self)
{
  game_texture_t *texture;
  (void)mrb;
  texture = (game_texture_t *)mrb_data_get_ptr(mrb, self, &game_texture_type);
  return mrb_fixnum_value(game_texture_width(texture));
}

static mrb_value
texture_height(mrb_state *mrb, mrb_value self)
{
  game_texture_t *texture;
  (void)mrb;
  texture = (game_texture_t *)mrb_data_get_ptr(mrb, self, &game_texture_type);
  return mrb_fixnum_value(game_texture_height(texture));
}

static mrb_value
texture_checksum(mrb_state *mrb, mrb_value self)
{
  game_texture_t *texture = mrb_data_get_ptr(mrb, self, &game_texture_type);
  return mrb_int_value(mrb, game_texture_checksum(texture));
}

static mrb_value
font_load(mrb_state *mrb, mrb_value klass)
{
  const char *path;
  game_font_t *font;

  mrb_get_args(mrb, "z", &path);
  mrb_value object = mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), NULL, &game_font_type));
  font = game_font_load(path);
  if (font == NULL) { collect_assets(mrb); font = game_font_load(path); }
  if (font == NULL) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "font asset load failed");
  }
  DATA_PTR(object) = font;
  return object;
}

static mrb_value
font_measure(mrb_state *mrb, mrb_value self)
{
  const char *text;
  mrb_int pixel_size;
  mrb_int spacing = 0;
  game_font_t *font;
  int measured;

  mrb_get_args(mrb, "zi|i", &text, &pixel_size, &spacing);
  if (pixel_size <= 0 || pixel_size > UINT16_MAX || spacing < INT_MIN ||
      spacing > INT_MAX) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid font size or spacing");
  }
  font = (game_font_t *)mrb_data_get_ptr(mrb, self, &game_font_type);
  measured = game_font_measure(font, text, (uint16_t)pixel_size, (int)spacing);
  if (measured < 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "font size is not present in the asset");
  }
  return mrb_fixnum_value(measured);
}

static mrb_value
font_draw(mrb_state *mrb, mrb_value self)
{
  mrb_int x, y, pixel_size, spacing = 0, color = 0xffff;
  const char *text;
  game_font_t *font;

  mrb_get_args(mrb, "ziii|ii", &text, &x, &y, &pixel_size, &spacing, &color);
  if (pixel_size <= 0 || pixel_size > UINT16_MAX || color < 0 ||
      color > 0xffff) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid font size or color");
  }
  font = (game_font_t *)mrb_data_get_ptr(mrb, self, &game_font_type);
  if (!game_display_draw_font(font, (int)x, (int)y, text,
                              (uint16_t)pixel_size, (int)spacing,
                              (uint16_t)color)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "font size is not present in the asset");
  }
  return mrb_nil_value();
}

static mrb_value
audio_dial_supported(mrb_state *mrb, mrb_value self)
{
  return mrb_bool_value(game_audio_dial_supported());
}

static mrb_value
audio_dial_value(mrb_state *mrb, mrb_value self)
{
  return game_audio_dial_supported() ? mrb_fixnum_value(game_audio_dial_value()) : mrb_nil_value();
}

static mrb_value
audio_master_volume(mrb_state *mrb, mrb_value self)
{
  return mrb_fixnum_value(game_audio_master_volume());
}

static mrb_value
audio_play(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_int volume = 255;
  game_audio_sound_t *sound;
  (void)self;
  mrb_get_args(mrb, "z|i", &path, &volume);
  if (volume < 0 || volume > 255) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "audio volume must be 0..255");
  }
  sound = game_audio_sound_load_cached(path);
  if (sound == NULL) { collect_assets(mrb); sound = game_audio_sound_load_cached(path); }
  if (sound == NULL || !game_audio_sound_play(sound, (uint8_t)volume)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "audio asset load or playback failed");
  }
  return mrb_true_value();
}

static mrb_value
audio_play_bgm(mrb_state *mrb, mrb_value self)
{
  const char *path;
  mrb_int volume = 255;
  mrb_bool loop = FALSE;
  game_audio_sound_t *sound;
  (void)self;
  mrb_get_args(mrb, "z|ib", &path, &volume, &loop);
  if (volume < 0 || volume > 255) mrb_raise(mrb, E_ARGUMENT_ERROR, "audio volume must be 0..255");
  sound = game_audio_sound_load_cached(path);
  if (sound == NULL) { collect_assets(mrb); sound = game_audio_sound_load_cached(path); }
  if (sound == NULL || !game_audio_sound_play_bgm(sound, (uint8_t)volume, loop))
    mrb_raise(mrb, E_RUNTIME_ERROR, "audio asset load or playback failed");
  return mrb_true_value();
}

static mrb_value
audio_stop_bgm(mrb_state *mrb, mrb_value self)
{
  (void)mrb; (void)self;
  game_audio_bgm_stop();
  return mrb_nil_value();
}

static mrb_value
audio_sound_play_bgm(mrb_state *mrb, mrb_value self)
{
  mrb_int volume = 255;
  mrb_bool loop = FALSE;
  mrb_get_args(mrb, "|ib", &volume, &loop);
  if (volume < 0 || volume > 255) mrb_raise(mrb, E_ARGUMENT_ERROR, "audio volume must be 0..255");
  game_audio_sound_t *sound = (game_audio_sound_t *)mrb_data_get_ptr(mrb, self, &game_audio_sound_type);
  if (!game_audio_sound_play_bgm(sound, (uint8_t)volume, loop)) mrb_raise(mrb, E_RUNTIME_ERROR, "audio playback failed");
  return mrb_true_value();
}

static mrb_value
audio_sound_load(mrb_state *mrb, mrb_value klass)
{
  const char *path;
  game_audio_sound_t *sound;

  mrb_get_args(mrb, "z", &path);
  mrb_value object = mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), NULL, &game_audio_sound_type));
  sound = game_audio_sound_load(path);
  if (sound == NULL) { collect_assets(mrb); sound = game_audio_sound_load(path); }
  if (sound == NULL) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "audio asset load failed");
  }
  DATA_PTR(object) = sound;
  return object;
}

static mrb_value
audio_sound_play(mrb_state *mrb, mrb_value self)
{
  mrb_int volume = 255;
  game_audio_sound_t *sound;

  mrb_get_args(mrb, "|i", &volume);
  if (volume < 0 || volume > 255) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "audio volume must be 0..255");
  }
  sound = (game_audio_sound_t *)mrb_data_get_ptr(mrb, self,
                                                  &game_audio_sound_type);
  if (!game_audio_sound_play(sound, (uint8_t)volume)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "audio playback failed");
  }
  return mrb_true_value();
}

static mrb_value
audio_sound_stop(mrb_state *mrb, mrb_value self)
{
  game_audio_sound_t *sound;
  (void)mrb;
  sound = (game_audio_sound_t *)mrb_data_get_ptr(mrb, self,
                                                  &game_audio_sound_type);
  game_audio_sound_stop(sound);
  return mrb_nil_value();
}

static mrb_value
audio_stop(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  game_audio_stop();
  return mrb_nil_value();
}

static mrb_value
audio_busy(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_bool_value(game_audio_busy());
}

static mrb_value
audio_supported(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_true_value();
}

static mrb_value
collision_rects_overlap(mrb_state *mrb, mrb_value self)
{
  mrb_int x1, y1, width1, height1;
  mrb_int x2, y2, width2, height2;
  int64_t right1, bottom1, right2, bottom2;

  (void)self;
  mrb_get_args(mrb, "iiiiiiii", &x1, &y1, &width1, &height1,
               &x2, &y2, &width2, &height2);
  if (width1 <= 0 || height1 <= 0 || width2 <= 0 || height2 <= 0) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "rectangle width and height must be positive");
  }

  right1 = (int64_t)x1 + (int64_t)width1;
  bottom1 = (int64_t)y1 + (int64_t)height1;
  right2 = (int64_t)x2 + (int64_t)width2;
  bottom2 = (int64_t)y2 + (int64_t)height2;
  return mrb_bool_value((int64_t)x1 < right2 && (int64_t)x2 < right1 &&
                        (int64_t)y1 < bottom2 && (int64_t)y2 < bottom1);
}

static mrb_value
runtime_backend(mrb_state *mrb, mrb_value self)
{
  (void)self;
  return mrb_str_new_lit(mrb, "rp2350");
}

static mrb_value
runtime_api_version(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value(GAME_API_VERSION);
}

static mrb_int
runtime_capability_mask(void)
{
  uint32_t capabilities = GAME_CAPABILITY_DISPLAY |
                          GAME_CAPABILITY_INPUT |
                          GAME_CAPABILITY_ASSETS |
                          GAME_CAPABILITY_SPRITES |
                          GAME_CAPABILITY_AUDIO |
                          GAME_CAPABILITY_COLLISION |
                          GAME_CAPABILITY_TEXTURES |
                          GAME_CAPABILITY_FONTS |
                          GAME_CAPABILITY_SHAPES |
                          GAME_CAPABILITY_AUDIO_MIXING;
  if (game_audio_dial_supported()) capabilities |= GAME_CAPABILITY_AUDIO_DIAL;
  if (game_3d_supported()) {
    capabilities |= GAME_CAPABILITY_THREE_D | GAME_CAPABILITY_THREE_D_MODELS;
  }
  if (game_storage_save_supported()) {
    capabilities |= GAME_CAPABILITY_SAVE;
  }
  return (mrb_int)capabilities;
}

static mrb_value
runtime_capabilities(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value(runtime_capability_mask());
}

static mrb_value
runtime_capability(mrb_state *mrb, mrb_value self)
{
  mrb_int capability;
  const mrb_int supported = runtime_capability_mask();
  (void)self;
  mrb_get_args(mrb, "i", &capability);
  return mrb_bool_value(capability > 0 && (capability & supported) == capability);
}

static mrb_value
assets_read(mrb_state *mrb, mrb_value self)
{
  const char *path;
  size_t size;
  mrb_value result;
  (void)self;

  mrb_get_args(mrb, "z", &path);
  if (!game_storage_file_size(path, &size)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "asset not found or SD card is unavailable");
  }
  if (size > (size_t)MRB_INT_MAX) {
    mrb_raise(mrb, E_RANGE_ERROR, "asset is too large");
  }

  result = mrb_str_new_capa(mrb, (mrb_int)size);
  mrb_str_resize(mrb, result, (mrb_int)size);
  if (!game_storage_read_file(path, (uint8_t *)RSTRING_PTR(result), size, &size)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "asset read failed");
  }
  if ((mrb_int)size != RSTRING_LEN(result)) {
    mrb_str_resize(mrb, result, (mrb_int)size);
  }
  return result;
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

static void
require_save_slot(mrb_state *mrb, const char *slot)
{
  if (!save_slot_valid(slot)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR,
              "save slot must be 1..24 ASCII letters, numbers, _ or -");
  }
}

static mrb_value
save_supported(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_bool_value(game_storage_save_supported());
}

static mrb_value
save_exists(mrb_state *mrb, mrb_value self)
{
  const char *slot;
  (void)self;
  mrb_get_args(mrb, "z", &slot);
  require_save_slot(mrb, slot);
  return mrb_bool_value(game_storage_save_exists(slot));
}

static mrb_value
save_read(mrb_state *mrb, mrb_value self)
{
  const char *slot;
  size_t size;
  mrb_value result;
  (void)self;

  mrb_get_args(mrb, "z", &slot);
  require_save_slot(mrb, slot);
  if (!game_storage_save_supported()) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save storage is unavailable");
  }
  if (!game_storage_save_exists(slot)) {
    return mrb_nil_value();
  }
  if (!game_storage_save_file_size(slot, &size)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save metadata read failed");
  }
  if (size > GAME_SAVE_MAX_BYTES || size > (size_t)MRB_INT_MAX) {
    mrb_raise(mrb, E_RANGE_ERROR, "save data is too large");
  }
  result = mrb_str_new_capa(mrb, (mrb_int)size);
  mrb_str_resize(mrb, result, (mrb_int)size);
  if (!game_storage_save_read(slot, (uint8_t *)RSTRING_PTR(result), size,
                              &size)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save data read failed");
  }
  if ((mrb_int)size != RSTRING_LEN(result)) {
    mrb_str_resize(mrb, result, (mrb_int)size);
  }
  return result;
}

static mrb_value
save_write(mrb_state *mrb, mrb_value self)
{
  const char *slot;
  mrb_value data;
  (void)self;

  mrb_get_args(mrb, "zS", &slot, &data);
  require_save_slot(mrb, slot);
  if ((size_t)RSTRING_LEN(data) > GAME_SAVE_MAX_BYTES) {
    mrb_raise(mrb, E_RANGE_ERROR, "save data must be at most 32768 bytes");
  }
  if (!game_storage_save_supported()) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save storage is unavailable");
  }
  if (!game_storage_save_write(slot, (const uint8_t *)RSTRING_PTR(data),
                               (size_t)RSTRING_LEN(data))) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save data write failed");
  }
  return mrb_true_value();
}

static mrb_value
save_delete(mrb_state *mrb, mrb_value self)
{
  const char *slot;
  (void)self;
  mrb_get_args(mrb, "z", &slot);
  require_save_slot(mrb, slot);
  if (!game_storage_save_supported()) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save storage is unavailable");
  }
  if (!game_storage_save_delete(slot)) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "save data delete failed");
  }
  return mrb_true_value();
}

static mrb_value
game_tick(mrb_state *mrb, mrb_value self)
{
  mrb_int fps;
  (void)self;
  mrb_get_args(mrb, "i", &fps);
  if (fps <= 0 || fps > 1000) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "fps must be between 1 and 1000");
  }
  game_frame_tick((uint32_t)fps);
  return mrb_nil_value();
}

static mrb_value
time_millis(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value((mrb_int)game_time_millis());
}

static mrb_value
time_delta_ms(mrb_state *mrb, mrb_value self)
{
  (void)mrb;
  (void)self;
  return mrb_fixnum_value((mrb_int)game_time_delta_ms());
}

void
mrb_picoruby_game_gem_init(mrb_state *mrb)
{
  struct RClass *game = mrb_define_module_id(mrb, MRB_SYM(Game));
  game_input_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Input));
  game_display_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Display));
  game_3d_module = mrb_define_module_under_id(mrb, game,
                                               mrb_intern_lit(mrb, "ThreeD"));
  game_model_init(mrb, game_3d_module);
  game_assets_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Assets));
  game_sprite_class = mrb_define_class_under_id(mrb, game, MRB_SYM(Sprite),
                                                 mrb->object_class);
  game_texture_class = mrb_define_class_under_id(mrb, game, MRB_SYM(Texture),
                                                  mrb->object_class);
  game_font_class = mrb_define_class_under_id(mrb, game,
                                               mrb_intern_lit(mrb, "Font"),
                                               mrb->object_class);
  game_audio_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Audio));
  game_audio_sound_class = mrb_define_class_under_id(
    mrb, game_audio_module, mrb_intern_lit(mrb, "Sound"), mrb->object_class);
  game_time_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Time));
  game_runtime_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Runtime));
  game_save_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Save));
  game_collision_module = mrb_define_module_under_id(mrb, game, MRB_SYM(Collision));
  MRB_SET_INSTANCE_TT(game_sprite_class, MRB_TT_CDATA);
  MRB_SET_INSTANCE_TT(game_texture_class, MRB_TT_CDATA);
  MRB_SET_INSTANCE_TT(game_font_class, MRB_TT_CDATA);
  MRB_SET_INSTANCE_TT(game_audio_sound_class, MRB_TT_CDATA);

  {
    struct RClass *capabilities = mrb_define_module_under_id(mrb, game,
                                                               MRB_SYM(Capabilities));
    mrb_define_const(mrb, game, "API_VERSION", mrb_fixnum_value(GAME_API_VERSION));
    mrb_define_const(mrb, capabilities, "DISPLAY",
                     mrb_fixnum_value(GAME_CAPABILITY_DISPLAY));
    mrb_define_const(mrb, capabilities, "INPUT",
                     mrb_fixnum_value(GAME_CAPABILITY_INPUT));
    mrb_define_const(mrb, capabilities, "ASSETS",
                     mrb_fixnum_value(GAME_CAPABILITY_ASSETS));
    mrb_define_const(mrb, capabilities, "SPRITES",
                     mrb_fixnum_value(GAME_CAPABILITY_SPRITES));
    mrb_define_const(mrb, capabilities, "AUDIO",
                     mrb_fixnum_value(GAME_CAPABILITY_AUDIO));
    mrb_define_const(mrb, capabilities, "SAVE",
                     mrb_fixnum_value(GAME_CAPABILITY_SAVE));
    mrb_define_const(mrb, capabilities, "COLLISION",
                     mrb_fixnum_value(GAME_CAPABILITY_COLLISION));
    mrb_define_const(mrb, capabilities, "TEXTURES",
                     mrb_fixnum_value(GAME_CAPABILITY_TEXTURES));
    mrb_define_const(mrb, capabilities, "FONTS",
                     mrb_fixnum_value(GAME_CAPABILITY_FONTS));
    mrb_define_const(mrb, capabilities, "SHAPES",
                     mrb_fixnum_value(GAME_CAPABILITY_SHAPES));
    mrb_define_const(mrb, capabilities, "AUDIO_DIAL",
                     mrb_fixnum_value(GAME_CAPABILITY_AUDIO_DIAL));
    mrb_define_const(mrb, capabilities, "AUDIO_MIXING",
                     mrb_fixnum_value(GAME_CAPABILITY_AUDIO_MIXING));
    mrb_define_const(mrb, capabilities, "THREE_D",
                     mrb_fixnum_value(GAME_CAPABILITY_THREE_D));
    mrb_define_const(mrb, capabilities, "THREE_D_MODELS",
                     mrb_fixnum_value(GAME_CAPABILITY_THREE_D_MODELS));
  }

  mrb_define_module_function_id(mrb, game, MRB_SYM(init), game_init, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game, MRB_SYM(tick), game_tick, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_input_module, MRB_SYM(state), input_state, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_input_module, MRB_SYM_Q(pressed), input_pressed,
                                MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_input_module, MRB_SYM_Q(just_pressed),
                                input_just_pressed, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_input_module, MRB_SYM_Q(just_released),
                                input_just_released, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(width), display_width, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(height), display_height, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(clear), display_clear, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(set_background),
                                display_set_background, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(begin_frame),
                                display_begin_frame, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(fill_rect), display_fill_rect, MRB_ARGS_ARG(5, 1));
  mrb_define_module_function_id(mrb, game_display_module,
                                mrb_intern_lit(mrb, "draw_circle"),
                                display_draw_circle, MRB_ARGS_ARG(4, 1));
  mrb_define_module_function_id(mrb, game_display_module,
                                mrb_intern_lit(mrb, "draw_line"),
                                display_draw_line, MRB_ARGS_ARG(6, 1));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(blit_rgb565), display_blit_rgb565, MRB_ARGS_REQ(5));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(blit_rgb565_region),
                                display_blit_rgb565_region, MRB_ARGS_REQ(8));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(text), display_text,
                                MRB_ARGS_ARG(3, 1));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(draw_text), display_text,
                                MRB_ARGS_ARG(3, 1));
  mrb_define_module_function_id(mrb, game_display_module, MRB_SYM(present), display_present, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "begin_frame"),
                                three_d_begin_frame, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "set_viewport"),
                                three_d_set_viewport, MRB_ARGS_REQ(4));
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "set_resolution"),
                                three_d_set_resolution, MRB_ARGS_REQ(2));
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "set_camera"),
                                three_d_set_camera, MRB_ARGS_ARG(5, 3));
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "draw_triangle"),
                                three_d_draw_triangle, MRB_ARGS_REQ(10));
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "draw_triangles"),
                                three_d_draw_triangles, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "draw_indexed_mesh"),
                                three_d_draw_indexed_mesh, MRB_ARGS_REQ(3));
  mrb_define_module_function_id(mrb, game_3d_module,
                                mrb_intern_lit(mrb, "draw_wireframe"),
                                three_d_draw_wireframe, MRB_ARGS_ARG(3, 1));
  mrb_define_module_function_id(mrb, game_assets_module, MRB_SYM(read), assets_read, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_assets_module, MRB_SYM(collect), assets_collect, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_assets_module, MRB_SYM(resident_bytes), assets_resident_bytes, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_save_module, MRB_SYM_Q(supported),
                                save_supported, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_save_module, MRB_SYM_Q(exists),
                                save_exists, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_save_module, MRB_SYM(read),
                                save_read, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_save_module, MRB_SYM(write),
                                save_write, MRB_ARGS_REQ(2));
  mrb_define_module_function_id(mrb, game_save_module, MRB_SYM(delete),
                                save_delete, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, game_collision_module,
                                MRB_SYM_Q(rects_overlap),
                                collision_rects_overlap, MRB_ARGS_REQ(8));
  mrb_define_class_method_id(mrb, game_sprite_class, MRB_SYM(load), sprite_load,
                             MRB_ARGS_ARG(4, 1));
  mrb_define_method_id(mrb, game_sprite_class, MRB_SYM(draw), sprite_draw,
                       MRB_ARGS_REQ(3));
  mrb_define_method_id(mrb, game_sprite_class, mrb_intern_lit(mrb, "draw_batch"),
                       sprite_draw_batch, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, game_sprite_class, MRB_SYM(width), sprite_width,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, game_sprite_class, MRB_SYM(height), sprite_height,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, game_sprite_class, MRB_SYM(frames), sprite_frames,
                       MRB_ARGS_NONE());
  mrb_define_class_method_id(mrb, game_texture_class, MRB_SYM(load), texture_load,
                             MRB_ARGS_ARG(3, 1));
  mrb_define_method_id(mrb, game_texture_class, MRB_SYM(draw), texture_draw,
                       MRB_ARGS_ARG(2, 6));
  mrb_define_method_id(mrb, game_texture_class, MRB_SYM(width), texture_width,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, game_texture_class, MRB_SYM(height), texture_height,
                       MRB_ARGS_NONE());
  mrb_define_method_id(mrb, game_texture_class, MRB_SYM(checksum), texture_checksum, MRB_ARGS_NONE());
  mrb_define_class_method_id(mrb, game_font_class, MRB_SYM(load), font_load,
                             MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, game_font_class, mrb_intern_lit(mrb, "measure"),
                       font_measure,
                       MRB_ARGS_ARG(2, 1));
  mrb_define_method_id(mrb, game_font_class, MRB_SYM(draw), font_draw,
                       MRB_ARGS_ARG(4, 2));
  mrb_define_module_function_id(mrb, game_audio_module, mrb_intern_lit(mrb, "dial_supported?"),
                                audio_dial_supported, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_audio_module, mrb_intern_lit(mrb, "dial_value"),
                                audio_dial_value, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_audio_module, mrb_intern_lit(mrb, "master_volume"),
                                audio_master_volume, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_audio_module, MRB_SYM(play), audio_play,
                                MRB_ARGS_ARG(1, 1));
  mrb_define_module_function_id(mrb, game_audio_module, mrb_intern_lit(mrb, "play_bgm"), audio_play_bgm, MRB_ARGS_ARG(1, 2));
  mrb_define_module_function_id(mrb, game_audio_module, mrb_intern_lit(mrb, "play_se"), audio_play, MRB_ARGS_ARG(1, 1));
  mrb_define_module_function_id(mrb, game_audio_module, mrb_intern_lit(mrb, "stop_bgm"), audio_stop_bgm, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_audio_module, MRB_SYM(stop), audio_stop,
                                MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_audio_module, MRB_SYM_Q(busy), audio_busy,
                                MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_time_module, MRB_SYM(millis), time_millis,
                                MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_time_module, MRB_SYM(delta_ms), time_delta_ms,
                                MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_audio_module, MRB_SYM_Q(supported),
                                audio_supported, MRB_ARGS_NONE());
  mrb_define_class_method_id(mrb, game_audio_sound_class,
                             mrb_intern_lit(mrb, "load"), audio_sound_load,
                             MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, game_audio_sound_class, MRB_SYM(play),
                       audio_sound_play, MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, game_audio_sound_class, mrb_intern_lit(mrb, "play_bgm"), audio_sound_play_bgm, MRB_ARGS_OPT(2));
  mrb_define_method_id(mrb, game_audio_sound_class, mrb_intern_lit(mrb, "play_se"), audio_sound_play, MRB_ARGS_OPT(1));
  mrb_define_method_id(mrb, game_audio_sound_class, MRB_SYM(stop),
                       audio_sound_stop, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_runtime_module, MRB_SYM(backend),
                                runtime_backend, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_runtime_module, MRB_SYM(api_version),
                                runtime_api_version, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_runtime_module, MRB_SYM(capabilities),
                                runtime_capabilities, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, game_runtime_module, MRB_SYM_Q(capability),
                                runtime_capability, MRB_ARGS_REQ(1));

  mrb_define_const(mrb, game_input_module, "UP", mrb_fixnum_value(GAME_BUTTON_UP));
  mrb_define_const(mrb, game_input_module, "DOWN", mrb_fixnum_value(GAME_BUTTON_DOWN));
  mrb_define_const(mrb, game_input_module, "LEFT", mrb_fixnum_value(GAME_BUTTON_LEFT));
  mrb_define_const(mrb, game_input_module, "RIGHT", mrb_fixnum_value(GAME_BUTTON_RIGHT));
  mrb_define_const(mrb, game_input_module, "A", mrb_fixnum_value(GAME_BUTTON_A));
  mrb_define_const(mrb, game_input_module, "B", mrb_fixnum_value(GAME_BUTTON_B));
  mrb_define_const(mrb, game_input_module, "L", mrb_fixnum_value(GAME_BUTTON_L));
  mrb_define_const(mrb, game_input_module, "R", mrb_fixnum_value(GAME_BUTTON_R));
  mrb_define_const(mrb, game_input_module, "START", mrb_fixnum_value(GAME_BUTTON_START));
  mrb_define_const(mrb, game_input_module, "SELECT", mrb_fixnum_value(GAME_BUTTON_SELECT));
}

void
mrb_picoruby_game_gem_final(mrb_state *mrb)
{
  (void)mrb;
}
