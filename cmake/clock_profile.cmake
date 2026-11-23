# Clock profiles are deliberately limited to the board that has been evaluated.
# Set these before pico_sdk_init(): boot stage 2 is a separate target and must
# receive the Flash divider as well as the application's SDK sources.
set(GAME_CLOCK_PROFILE stock CACHE STRING "Clock profile: stock or weact-{150,200,250,300}")
set_property(CACHE GAME_CLOCK_PROFILE PROPERTY STRINGS stock weact-150 weact-200 weact-250 weact-300)
option(GAME_CLOCK_DIAGNOSTICS "Run full scratch PSRAM checks before boot (PiZero or WeAct profiles)" OFF)

if(GAME_CLOCK_DIAGNOSTICS AND GAME_CLOCK_PROFILE STREQUAL "stock" AND
   NOT PICO_BOARD STREQUAL "waveshare_rp2350_pizero")
  message(FATAL_ERROR "GAME_CLOCK_DIAGNOSTICS requires a WeAct clock profile or PiZero")
endif()

if(GAME_CLOCK_DIAGNOSTICS)
  set(_clock_diagnostics 1)
else()
  set(_clock_diagnostics 0)
endif()

if(GAME_DVI)
  if(NOT PICO_BOARD STREQUAL "waveshare_rp2350_pizero" OR
     NOT GAME_CLOCK_PROFILE STREQUAL "stock")
    message(FATAL_ERROR "DVI requires Waveshare RP2350-PiZero and its dedicated clock profile")
  endif()
  # PicoDVI shifts one TMDS bit per sys_clk cycle for 25.2 MHz VGA timing.
  set(_clock_profile_name pizero-dvi)
  set(_clock_mhz 252)
  set(_clock_vco 1512000000)
  set(_clock_postdiv1 6)
  set(_clock_postdiv2 1)
  set(_clock_voltage VREG_VOLTAGE_1_20)
  set(_clock_mv 1200)
elseif(GAME_CLOCK_PROFILE STREQUAL "stock")
  add_compile_definitions(GAME_CLOCK_PROFILE_NAME="stock" GAME_CLOCK_CUSTOM=0
    GAME_CLOCK_DIAGNOSTICS=${_clock_diagnostics})
  return()
else()
  if(NOT PICO_BOARD STREQUAL "weact_studio_rp2350b_core")
    message(FATAL_ERROR "Clock profiles require WeAct RP2350B Core")
  endif()
  if(GAME_CLOCK_PROFILE STREQUAL "weact-150")
    set(_clock_mhz 150)
    set(_clock_vco 1500000000)
    set(_clock_postdiv1 5)
    set(_clock_postdiv2 2)
    set(_clock_voltage VREG_VOLTAGE_1_10)
    set(_clock_mv 1100)
  elseif(GAME_CLOCK_PROFILE STREQUAL "weact-200")
    set(_clock_mhz 200)
    set(_clock_vco 1200000000)
    set(_clock_postdiv1 3)
    set(_clock_postdiv2 2)
    set(_clock_voltage VREG_VOLTAGE_1_15)
    set(_clock_mv 1150)
  elseif(GAME_CLOCK_PROFILE STREQUAL "weact-250")
    set(_clock_mhz 250)
    set(_clock_vco 1500000000)
    set(_clock_postdiv1 3)
    set(_clock_postdiv2 2)
    set(_clock_voltage VREG_VOLTAGE_1_20)
    set(_clock_mv 1200)
  elseif(GAME_CLOCK_PROFILE STREQUAL "weact-300")
    set(_clock_mhz 300)
    set(_clock_vco 1500000000)
    set(_clock_postdiv1 5)
    set(_clock_postdiv2 1)
    set(_clock_voltage VREG_VOLTAGE_1_25)
    set(_clock_mv 1250)
  else()
    message(FATAL_ERROR "Unknown GAME_CLOCK_PROFILE: ${GAME_CLOCK_PROFILE}")
  endif()
  set(_clock_profile_name "${GAME_CLOCK_PROFILE}")
endif()

math(EXPR _clock_hz "${_clock_mhz} * 1000000")
math(EXPR _flash_div "(${_clock_mhz} + 74) / 75")
add_compile_definitions(
  GAME_CLOCK_PROFILE_NAME="${_clock_profile_name}"
  GAME_CLOCK_CUSTOM=1
  GAME_CLOCK_DIAGNOSTICS=${_clock_diagnostics}
  GAME_CLOCK_MIN_MV=${_clock_mv}
  SYS_CLK_HZ=${_clock_hz}
  PLL_SYS_REFDIV=1
  PLL_SYS_VCO_FREQ_HZ=${_clock_vco}
  PLL_SYS_POSTDIV1=${_clock_postdiv1}
  PLL_SYS_POSTDIV2=${_clock_postdiv2}
  SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=1
  SYS_CLK_VREG_VOLTAGE_MIN=${_clock_voltage}
  PICO_FLASH_SPI_CLKDIV=${_flash_div}
  PICO_FLASH_SPI_RXDELAY=${_flash_div}
  PICO_DEFAULT_PSRAM_MAX_FREQ=75000000
)
