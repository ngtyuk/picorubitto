#include <mruby.h>
#include <mruby/array.h>
#include <mruby/presym.h>
#include <mruby/string.h>

/* Keep logging independent of IO/File. Kernel.print already owns the target's
 * stdout transport. Match mruby-io's bounded array traversal on both targets. */
static void
console_print(mrb_state *mrb, mrb_value string)
{
  mrb_funcall_id(mrb, mrb_obj_value(mrb->kernel_module), MRB_SYM(print), 1, string);
}

static void
console_puts_value(mrb_state *mrb, mrb_value value, int depth)
{
  if (mrb_array_p(value)) {
    mrb_int length = RARRAY_LEN(value);
    if (depth >= 16) {
      console_print(mrb, mrb_str_new_lit(mrb, "[...]\n"));
    } else if (length == 0) {
      console_print(mrb, mrb_str_new_lit(mrb, "\n"));
    } else {
      for (mrb_int i = 0; i < length && i < RARRAY_LEN(value); i++) {
        int arena = mrb_gc_arena_save(mrb);
        console_puts_value(mrb, mrb_ary_ref(mrb, value, i), depth + 1);
        mrb_gc_arena_restore(mrb, arena);
      }
    }
  } else {
    mrb_value string = mrb_obj_as_string(mrb, value);
    mrb_int length = RSTRING_LEN(string);
    mrb_bool newline = length == 0 || RSTRING_PTR(string)[length - 1] != '\n';
    console_print(mrb, string);
    if (newline) console_print(mrb, mrb_str_new_lit(mrb, "\n"));
  }
}

static mrb_value
console_puts(mrb_state *mrb, mrb_value self)
{
  mrb_value *values;
  mrb_int count;
  (void)self;
  mrb_get_args(mrb, "*", &values, &count);
  if (count == 0) {
    console_print(mrb, mrb_str_new_lit(mrb, "\n"));
    return mrb_nil_value();
  }
  /* Calling print/to_s may grow the VM stack; do not retain its argv pointer. */
  mrb_value arguments = mrb_ary_new_from_values(mrb, count, values);
  for (mrb_int i = 0; i < count; i++) {
    int arena = mrb_gc_arena_save(mrb);
    console_puts_value(mrb, mrb_ary_ref(mrb, arguments, i), 0);
    mrb_gc_arena_restore(mrb, arena);
  }
  return mrb_nil_value();
}

void
game_console_init(mrb_state *mrb)
{
  mrb_define_module_function_id(mrb, mrb->kernel_module, MRB_SYM(puts),
                                console_puts, MRB_ARGS_ANY());
}
