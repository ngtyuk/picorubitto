#include <math.h>
#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include "game_platform.h"

/* One GC-owned VM-heap allocation: immutable model data and reusable world
 * vertices. Drawing uses the renderer's existing camera-space workspace. */
struct game_model {
  size_t vertices, triangles, edges, bytes;
  uint16_t color;
  bool depth_test;
  float *source, *world;
  uint16_t *indices, *colors, *lines;
};
static size_t model_resident_bytes;

size_t game_model_resident_bytes(void) { return model_resident_bytes; }

static void model_free(mrb_state *mrb, void *ptr)
{
  struct game_model *model = ptr;
  if (model) {
    model_resident_bytes -= model->bytes;
    mrb_free(mrb, model);
  }
}
static const struct mrb_data_type model_type = { "Game::ThreeD::Model", model_free };

static float model_number(mrb_state *mrb, mrb_value value)
{
  double number;
  if (mrb_float_p(value)) number = mrb_float(value);
  else if (mrb_integer_p(value)) number = (double)mrb_integer(value);
  else {
    mrb_raise(mrb, E_TYPE_ERROR, "model values must be Integer or Float");
    return 0;
  }
  if (!isfinite(number) || fabs(number) > 32768.0)
    mrb_raise(mrb, E_ARGUMENT_ERROR, "model values must be finite and within +/-32768");
  return (float)number;
}

static void model_integers(mrb_state *mrb, mrb_value values, size_t limit)
{
  for (mrb_int i = 0; i < RARRAY_LEN(values); i++) {
    mrb_value v = mrb_ary_ref(mrb, values, i);
    if (!mrb_integer_p(v)) mrb_raise(mrb, E_TYPE_ERROR, "model indices and colors must be integers");
    if (mrb_integer(v) < 0 || (uint64_t)mrb_integer(v) >= limit)
      mrb_raise(mrb, E_ARGUMENT_ERROR, "model index or color is out of range");
  }
}

static mrb_value model_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value vertices, indices, colors, edges = mrb_nil_value();
  mrb_value color_value = mrb_int_value(mrb, 0xffff);
  mrb_value depth = mrb_true_value();
  mrb_get_args(mrb, "AAA|Aoo", &vertices, &indices, &colors, &edges, &color_value, &depth);
  if (DATA_PTR(self)) mrb_raise(mrb, E_RUNTIME_ERROR, "model is already initialized");
  if (!mrb_integer_p(color_value)) mrb_raise(mrb, E_TYPE_ERROR, "model color must be an integer");
  mrb_int color = mrb_integer(color_value);
  size_t nv = (size_t)RARRAY_LEN(vertices) / 3u;
  size_t nt = (size_t)RARRAY_LEN(indices) / 3u;
  size_t ne = mrb_nil_p(edges) ? 0 : (size_t)RARRAY_LEN(edges) / 2u;
  if (RARRAY_LEN(vertices) % 3 || nv == 0 || nv > 256 ||
      RARRAY_LEN(indices) % 3 || nt > 1024 || (size_t)RARRAY_LEN(colors) != nt ||
      (!mrb_nil_p(edges) && RARRAY_LEN(edges) % 2) || ne > 2048 ||
      color < 0 || color > 65535)
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid model arrays or limits");
  if (!mrb_true_p(depth) && !mrb_false_p(depth))
    mrb_raise(mrb, E_TYPE_ERROR, "model depth_test must be true or false");
  for (mrb_int i = 0; i < RARRAY_LEN(vertices); i++) model_number(mrb, mrb_ary_ref(mrb, vertices, i));
  model_integers(mrb, indices, nv);
  model_integers(mrb, colors, 65536);
  if (!mrb_nil_p(edges)) model_integers(mrb, edges, nv);
  size_t bytes = nv * 6u * sizeof(float) + (nt * 4u + ne * 2u) * sizeof(uint16_t);
  struct game_model *model = mrb_malloc(mrb, sizeof(*model) + bytes);
  *model = (struct game_model){ .vertices=nv, .triangles=nt, .edges=ne,
    .bytes=bytes, .color=(uint16_t)color, .depth_test=mrb_true_p(depth) };
  model->source = (float *)(model + 1);
  model->world = model->source + nv * 3u;
  model->indices = (uint16_t *)(model->world + nv * 3u);
  model->colors = model->indices + nt * 3u;
  model->lines = model->colors + nt;
  for (size_t i=0; i<nv*3u; i++) model->source[i] = model_number(mrb, mrb_ary_ref(mrb, vertices, i));
  for (size_t i=0; i<nt*3u; i++) model->indices[i] = (uint16_t)mrb_integer(mrb_ary_ref(mrb, indices, i));
  for (size_t i=0; i<nt; i++) model->colors[i] = (uint16_t)mrb_integer(mrb_ary_ref(mrb, colors, i));
  for (size_t i=0; i<ne*2u; i++) model->lines[i] = (uint16_t)mrb_integer(mrb_ary_ref(mrb, edges, i));
  model_resident_bytes += bytes;
  mrb_data_init(self, model, &model_type);
  return self;
}

static void model_transform(struct game_model *model, const float *t)
{
  float sy = sinf(t[3]), cy = cosf(t[3]);
  float sp = sinf(t[4]), cp = cosf(t[4]);
  float sr = sinf(t[5]), cr = cosf(t[5]);
  for (size_t i = 0; i < model->vertices * 3u; i += 3u) {
    float x = model->source[i] * t[6];
    float y = model->source[i+1] * t[7];
    float z = model->source[i+2] * t[8];
    float rx = x * cy - z * sy;
    float rz = x * sy + z * cy;
    float ry = y * cp - rz * sp;
    rz = y * sp + rz * cp;
    model->world[i] = t[0] + rx * cr - ry * sr;
    model->world[i+1] = t[1] + rx * sr + ry * cr;
    model->world[i+2] = t[2] + rz;
  }
}

static mrb_value model_draw_instances(mrb_state *mrb, mrb_value self)
{
  mrb_value values;
  mrb_get_args(mrb, "A", &values);
  struct game_model *model = mrb_data_get_ptr(mrb, self, &model_type);
  if (!model) mrb_raise(mrb, E_RUNTIME_ERROR, "model is not initialized");
  mrb_int length = RARRAY_LEN(values);
  if (length % 9 || length > 128 * 9)
    mrb_raise(mrb, E_ARGUMENT_ERROR, "instances must contain at most 128 nine-value transforms");
  /* Validate the complete batch before drawing any instance. */
  for (mrb_int i=0; i<length; i++) model_number(mrb, mrb_ary_ref(mrb, values, i));
  for (mrb_int offset=0; offset<length; offset+=9) {
    float transform[9];
    for (mrb_int i=0; i<9; i++) transform[i] = model_number(mrb, mrb_ary_ref(mrb, values, offset+i));
    model_transform(model, transform);
    /* Empty geometry still checks that a 3D frame was begun. */
    if (!game_3d_draw_model_geometry(model->world, model->vertices, model->indices,
          model->triangles, model->colors, model->lines, model->edges,
          model->color, model->depth_test))
      mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid model draw or frame not begun");
  }
  return mrb_nil_value();
}

static mrb_value model_copy(mrb_state *mrb, mrb_value self)
{
  (void)self;
  mrb_raise(mrb, E_TYPE_ERROR, "models cannot be copied; share the model instead");
  return mrb_nil_value();
}

void game_model_init(mrb_state *mrb, struct RClass *three_d)
{
  struct RClass *klass = mrb_define_class_under(mrb, three_d, "Model", mrb->object_class);
  MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
  mrb_define_method(mrb, klass, "initialize", model_initialize, MRB_ARGS_ARG(3, 3));
  mrb_define_method(mrb, klass, "draw_instances", model_draw_instances, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, klass, "initialize_copy", model_copy, MRB_ARGS_REQ(1));
  mrb_define_const(mrb, klass, "MAX_VERTICES", mrb_int_value(mrb, 256));
  mrb_define_const(mrb, klass, "MAX_TRIANGLES", mrb_int_value(mrb, 1024));
  mrb_define_const(mrb, klass, "MAX_EDGES", mrb_int_value(mrb, 2048));
  mrb_define_const(mrb, klass, "MAX_INSTANCES", mrb_int_value(mrb, 128));
}
