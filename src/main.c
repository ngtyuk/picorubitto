#include <pico/stdlib.h>

#include <stdio.h>
#include <string.h>

#include <mruby.h>
#include <mruby/debug.h>
#include <mruby/dump.h>

#include "estalloc_mruby.h"
#include "game_platform.h"
#include "version.h"

/* The Prism compiler owns this bridge pointer in PicoRuby 4.x. */
extern mrb_state *global_mrb;

static void
fatal_error(const char *title, const char *detail)
{
  printf("GAME ERROR: %s: %s\n", title, detail);
  game_display_error(title, detail);
  while (true) {
    tight_loop_contents();
  }
}

int
main(void)
{
  uint8_t *rom = NULL;
  size_t rom_size = 0;
  int result = 0;

  stdio_init_all();
  /* Give a USB CDC serial monitor time to open before the boot log. */
  sleep_ms(2000);
  printf("PicoRubitto boot\n");
  printf("PicoRuby %s\n", picorb_version_with_build_info());
  printf("VM: profile=%s scope=picoruby-mruby\n", GAME_VM_PROFILE);
  printf("STACK: core0=%u core1=%u bytes\n",
         (unsigned)PICO_STACK_SIZE, (unsigned)PICO_CORE1_STACK_SIZE);

  if (!game_platform_boot()) {
    fatal_error(game_platform_boot_error(), "PSRAM, TFT, or SD initialization failed");
  }

  if (!game_storage_load_rom(&rom, &rom_size)) {
    fatal_error("CARTRIDGE", "cannot load /" GAME_ROM_FILENAME);
  }

  printf(GAME_ROM_FILENAME ": %lu bytes\n", (unsigned long)rom_size);
  uint32_t rom_checksum = UINT32_C(2166136261);
  for (size_t index = 0; index < rom_size; index++) {
    rom_checksum = (rom_checksum ^ rom[index]) * UINT32_C(16777619);
  }
  printf("ROM: fnv1a=%08lx\n", (unsigned long)rom_checksum);
  printf("VM: initializing\n");
  uint32_t vm_started = to_ms_since_boot(get_absolute_time());
  mrb_state *mrb = mrb_open_with_custom_alloc(game_vm_heap(), game_vm_heap_size());
  if (mrb == NULL || mrb->exc != NULL) {
    fatal_error("VM", "cannot allocate PicoRuby VM in PSRAM");
  }
  global_mrb = mrb;
  printf("VM: ready init_ms=%lu; loading cartridge\n",
         (unsigned long)(to_ms_since_boot(get_absolute_time()) - vm_started));

  mrb_load_irep_buf(mrb, rom, rom_size);
  if (mrb->exc != NULL) {
    mrb_print_error(mrb);
    game_display_error("GAME", "Ruby exception; see USB serial");
    result = 1;
  }

  game_audio_stop();
  game_display_clear_background();
  mrb_close(mrb);
  game_assets_collect();
  global_mrb = NULL;
  printf("Game exited (%d)\n", result);
  while (true) {
    tight_loop_contents();
  }
}
