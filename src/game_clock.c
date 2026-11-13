#include "game_clock.h"
#include "game_platform.h"

#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/psram.h>
#include <hardware/regs/qmi.h>
#include <hardware/structs/qmi.h>
#include <hardware/sync.h>
#include <hardware/vreg.h>

void
game_clock_init(void)
{
#if GAME_CLOCK_CUSTOM
  /* Before stdio, SPI, audio or Core 1 starts. Keep clk_peri <= 150 MHz;
   * clk_usb and clk_adc retain the SDK's independent 48 MHz source. */
  uint32_t sys_hz = clock_get_hz(clk_sys);
  uint32_t divisor = (sys_hz + 149999999u) / 150000000u;
  clock_configure_int_divider(clk_peri, 0,
                             CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                             sys_hz, divisor);
#endif
}

void
game_clock_report(void)
{
#if GAME_CLOCK_CUSTOM
  uint32_t sys_hz = clock_get_hz(clk_sys);
  uint32_t flash_div = (qmi_hw->m[0].timing & QMI_M0_TIMING_CLKDIV_BITS) >>
                       QMI_M0_TIMING_CLKDIV_LSB;
  uint32_t psram_div = (qmi_hw->m[1].timing & QMI_M1_TIMING_CLKDIV_BITS) >>
                       QMI_M1_TIMING_CLKDIV_LSB;
  printf("CLOCK: profile=%s sys_hz=%lu peri_hz=%lu usb_hz=%lu adc_hz=%lu "
         "vreg_min_mv=%u vreg_sel=%u\n", GAME_CLOCK_PROFILE_NAME,
         (unsigned long)sys_hz, (unsigned long)clock_get_hz(clk_peri),
         (unsigned long)clock_get_hz(clk_usb),
         (unsigned long)clock_get_hz(clk_adc), GAME_CLOCK_MIN_MV,
         (unsigned)vreg_get_voltage());
  printf("CLOCK: flash_div=%lu flash_hz=%lu psram_div=%lu psram_hz=%lu\n",
         (unsigned long)flash_div,
         (unsigned long)(flash_div ? sys_hz / flash_div : 0),
         (unsigned long)psram_div,
         (unsigned long)(psram_div ? sys_hz / psram_div : 0));
#endif
}

bool
game_clock_check_psram(void)
{
#if GAME_CLOCK_DIAGNOSTICS
  /* Called before the VM, assets, framebuffer setup and Core 1. This is
   * destructive to scratch PSRAM only. Bypass XIP cache so readback reaches
   * the chip, including the tail of every page and the whole 8 MiB region. */
  if (!psram_is_available() || psram_get_size() < GAME_PSRAM_TOTAL_BYTES) {
    return false;
  }
  volatile uint32_t *memory = (volatile uint32_t *)(uintptr_t)0x15000000u;
  const size_t words = GAME_PSRAM_TOTAL_BYTES / sizeof(uint32_t);
  printf("CLOCK TEST: uncached PSRAM %u bytes, 2 address-pattern passes\n",
         (unsigned)GAME_PSRAM_TOTAL_BYTES);
  for (unsigned pass = 0; pass < 2; ++pass) {
    uint32_t seed = pass ? UINT32_MAX : 0;
    for (size_t index = 0; index < words; ++index) {
      memory[index] = ((uint32_t)index * UINT32_C(0x9e3779b9)) ^ seed;
    }
    __dmb();
    for (size_t index = 0; index < words; ++index) {
      uint32_t expected = ((uint32_t)index * UINT32_C(0x9e3779b9)) ^ seed;
      uint32_t actual = memory[index];
      if (actual != expected) {
        printf("CLOCK TEST: FAIL pass=%u offset=%lu expected=%08lx actual=%08lx\n",
               pass + 1, (unsigned long)(index * sizeof(uint32_t)),
               (unsigned long)expected, (unsigned long)actual);
        return false;
      }
    }
    printf("CLOCK TEST: pass=%u OK\n", pass + 1);
  }
  printf("CLOCK TEST: PASS (boot check; not a stability certification)\n");
#endif
  return true;
}
