#!/usr/bin/env python3
"""Exercise production clock setup with and without optional boot diagnostics."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
stub = r'''
#ifndef CLOCK_TEST_SDK_H
#define CLOCK_TEST_SDK_H
#include <stdbool.h>
#include <stdint.h>
enum { clk_sys, clk_peri, clk_usb, clk_adc };
#define CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS 0
#define QMI_M0_TIMING_CLKDIV_BITS 255u
#define QMI_M0_TIMING_CLKDIV_LSB 0
#define QMI_M1_TIMING_CLKDIV_BITS 255u
#define QMI_M1_TIMING_CLKDIV_LSB 0
typedef struct { struct { uint32_t timing; } m[2]; } mock_qmi;
extern mock_qmi *qmi_hw;
extern uint32_t test_clocks[4];
extern unsigned clock_calls, psram_calls;
static inline uint32_t clock_get_hz(int clock) { return test_clocks[clock]; }
static inline bool clock_configure_int_divider(int clock, unsigned src,
    unsigned aux, uint32_t source_hz, uint32_t divisor) {
  (void)src; (void)aux;
  clock_calls++;
  test_clocks[clock] = source_hz / divisor;
  return true;
}
static inline unsigned vreg_get_voltage(void) { return 14; }
static inline bool psram_is_available(void) { psram_calls++; return false; }
static inline uint32_t psram_get_size(void) { return 0; }
static inline void __dmb(void) {}
#endif
'''
harness = r'''
#include <assert.h>
#include <stdlib.h>
#include "stub.h"
#include "game_clock.h"
uint32_t test_clocks[4];
unsigned clock_calls, psram_calls;
mock_qmi registers = {{{4}, {4}}};
mock_qmi *qmi_hw = &registers;
int main(int argc, char **argv) {
  assert(argc == 2);
  uint32_t hz = (uint32_t)strtoul(argv[1], NULL, 10);
  test_clocks[clk_sys] = test_clocks[clk_peri] = hz;
  test_clocks[clk_usb] = test_clocks[clk_adc] = 48000000;
  game_clock_init();
#if GAME_CLOCK_CUSTOM
  assert(clock_calls == 1);
  assert(test_clocks[clk_peri] == hz / ((hz + 149999999u) / 150000000u));
  assert(test_clocks[clk_peri] <= 150000000);
#else
  assert(clock_calls == 0);
  assert(test_clocks[clk_peri] == hz);
#endif
  assert(test_clocks[clk_sys] == hz);
  assert(test_clocks[clk_usb] == 48000000);
  assert(test_clocks[clk_adc] == 48000000);
  game_clock_report();
#if GAME_CLOCK_DIAGNOSTICS
  assert(!game_clock_check_psram());
  assert(psram_calls == 1);
#else
  assert(game_clock_check_psram());
  assert(psram_calls == 0);
#endif
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='gaming-clock-runtime-') as directory:
    path = Path(directory)
    (path / 'stub.h').write_text(stub)
    (path / 'main.c').write_text(harness)
    for header in ['pico/stdlib.h', 'hardware/clocks.h', 'hardware/psram.h',
                   'hardware/regs/qmi.h', 'hardware/structs/qmi.h',
                   'hardware/sync.h', 'hardware/vreg.h']:
        target = path / header
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "stub.h"\n')
    for custom, diagnostics in [(0, 0), (1, 0), (1, 1)]:
        executable = path / f'clock-{custom}-{diagnostics}'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-I' + str(path), '-I' + str(root / 'include'),
            f'-DGAME_CLOCK_CUSTOM={custom}', f'-DGAME_CLOCK_DIAGNOSTICS={diagnostics}',
            '-DGAME_CLOCK_MIN_MV=1250', '-DGAME_CLOCK_PROFILE_NAME="weact-test"',
            str(root / 'src/game_clock.c'), str(path / 'main.c'), '-o', str(executable)], check=True)
        for hz in [150000000, 200000000, 250000000, 300000000]:
            result = subprocess.run([str(executable), str(hz)], capture_output=True, text=True, check=True)
            if custom:
                assert f'sys_hz={hz}' in result.stdout
                assert 'usb_hz=48000000 adc_hz=48000000' in result.stdout
            else:
                assert result.stdout == ''
print('clock runtime regression: PASS (stock, normal profiles, diagnostics; ASan/UBSan)')
