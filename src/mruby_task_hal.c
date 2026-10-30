#include <stdint.h>

#include <pico/stdlib.h>
#include <hardware/sync.h>

#include <mruby.h>

/* The game loop is single-threaded in v1. Keep mruby-task linkable without
 * importing the interactive machine/terminal port from PicoRuby. */
static uint32_t irq_state;
static uint32_t irq_nesting;

void
mrb_hal_task_init(mrb_state *mrb)
{
  (void)mrb;
  irq_state = 0;
  irq_nesting = 0;
}

void
mrb_hal_task_final(mrb_state *mrb)
{
  (void)mrb;
}

void
mrb_task_disable_irq(void)
{
  if (irq_nesting++ == 0) {
    irq_state = save_and_disable_interrupts();
  }
}

void
mrb_task_enable_irq(void)
{
  if (irq_nesting == 0) return;
  if (--irq_nesting == 0) {
    restore_interrupts(irq_state);
  }
}

void
mrb_hal_task_idle_cpu(mrb_state *mrb)
{
  (void)mrb;
  tight_loop_contents();
}

void
mrb_hal_task_sleep_us(mrb_state *mrb, mrb_int usec)
{
  (void)mrb;
  sleep_us((uint32_t)usec);
}
