#include <stdint.h>

/* FatFs needs this hook when write support is enabled. v1 is read-only from
 * the game API, but providing a deterministic timestamp keeps the filesystem
 * port linkable if a future cartridge manager creates files. */
uint32_t
get_fattime(void)
{
  return ((uint32_t)(2026 - 1980) << 25) |
         ((uint32_t)1 << 21) |
         ((uint32_t)1 << 16);
}
