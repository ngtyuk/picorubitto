/* Keep the upstream Estalloc heap without importing the interactive Machine
 * device port. Compiling here also includes its symbols in mruby's presyms. */
#include "picoruby-machine/src/heap.c"
#include "picoruby-machine/src/estalloc.c"
#include "picoruby-machine/src/mruby/alloc.c"
