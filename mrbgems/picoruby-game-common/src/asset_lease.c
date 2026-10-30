#include <mruby.h>

#if defined(__EMSCRIPTEN__)
#include <stdint.h>
#include <emscripten.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/presym.h>

/* Own only numeric IDs. The upstream JS::Object bridge does not release its
 * JS reference table on GC, so storing decoded assets there would retain them. */
EM_JS(void, release_game_asset, (int id), {
  if (globalThis.GameEmulator) globalThis.GameEmulator.releaseAssetHandle(id);
});

static void
lease_free(mrb_state *mrb, void *ptr)
{
  (void)mrb;
  if (ptr != NULL) release_game_asset((int)(uintptr_t)ptr);
}

static const struct mrb_data_type lease_type = { "Game::AssetLease", lease_free };

static mrb_value
lease_new(mrb_state *mrb, mrb_value klass)
{
  return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), NULL, &lease_type));
}

static mrb_value
lease_attach(mrb_state *mrb, mrb_value self)
{
  mrb_int id;
  mrb_get_args(mrb, "i", &id);
  if (id <= 0 || id > INT32_MAX || DATA_PTR(self) != NULL) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid asset lease");
  }
  DATA_PTR(self) = (void *)(uintptr_t)id;
  return self;
}

static mrb_value
lease_id(mrb_state *mrb, mrb_value self)
{
  return mrb_int_value(mrb, (mrb_int)(uintptr_t)DATA_PTR(self));
}
#endif

void
mrb_picoruby_game_common_gem_init(mrb_state *mrb)
{
#if defined(__EMSCRIPTEN__)
  struct RClass *game = mrb_define_module_id(mrb, MRB_SYM(Game));
  struct RClass *lease = mrb_define_class_under_id(mrb, game, MRB_SYM(AssetLease), mrb->object_class);
  MRB_SET_INSTANCE_TT(lease, MRB_TT_CDATA);
  mrb_define_class_method_id(mrb, lease, MRB_SYM(new), lease_new, MRB_ARGS_NONE());
  mrb_define_method_id(mrb, lease, MRB_SYM(attach), lease_attach, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, lease, MRB_SYM(id), lease_id, MRB_ARGS_NONE());
#else
  (void)mrb;
#endif
}

void
mrb_picoruby_game_common_gem_final(mrb_state *mrb)
{
  (void)mrb;
}
