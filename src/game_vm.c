#include "game_vm.h"

#include <stdbool.h>
#include "estalloc_mruby.h"

#if GAME_VM_STATE_SRAM
/* Keep the state, including method/IV/constant caches, in internal SRAM.
 * Objects, Ruby stacks, and buffers continue to use the PSRAM allocator. */
static mrb_state vm_state __attribute__((aligned(8)));
static bool state_requested;
static bool state_in_use;

void *__real_mrb_basic_alloc_func(void *ptr, size_t size);

/* GNU ld --wrap redirects all VM allocator references here. Only the first
 * allocation inside game_vm_open may claim the static state: an unrelated
 * allocation of sizeof(mrb_state) must still go to the ordinary heap. */
void *
__wrap_mrb_basic_alloc_func(void *ptr, size_t size)
{
  if (ptr == &vm_state) {
    if (size == 0) {
      state_in_use = false;
      return NULL;
    }
    /* A fixed state cannot grow. Failed realloc preserves its live contents. */
    return size <= sizeof(vm_state) ? ptr : NULL;
  }
  if (state_requested && ptr == NULL) {
    state_requested = false;
    /* Fail safely if an upstream change alters mrb_open_core's first request. */
    if (size != sizeof(vm_state)) return NULL;
    state_in_use = true;
    return &vm_state;
  }
  return __real_mrb_basic_alloc_func(ptr, size);
}
#endif

mrb_state *
game_vm_open(void *heap, size_t bytes)
{
#if GAME_VM_STATE_SRAM
  /* Reinitializing the shared allocator while a VM is alive corrupts its heap. */
  if (state_in_use || state_requested) return NULL;
  state_requested = true;
#endif
  mrb_state *mrb = mrb_open_with_custom_alloc(heap, bytes);
#if GAME_VM_STATE_SRAM
  state_requested = false;
#endif
  return mrb;
}
