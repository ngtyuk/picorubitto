#ifndef GAME_VM_H
#define GAME_VM_H

#include <stddef.h>
#include <mruby.h>

/* Open the console's single VM using the selected state placement. */
mrb_state *game_vm_open(void *heap, size_t bytes);

#endif
