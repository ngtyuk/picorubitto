cmake_minimum_required(VERSION 3.22)

set(PICO_BOARD pimoroni_pico_plus2_rp2350 CACHE STRING
  "Pico SDK board name (default: Pimoroni Pico Plus 2 RP2350)")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/pico-sdk" CACHE PATH "Pico SDK path")
set(PICORUBY_ROOT "" CACHE PATH "PicoRuby checkout")
set(PICORUBY_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/../mruby/pico-gaming" CACHE PATH "PicoRuby cross-build directory")
unset(GAME_TFT_DMA_FROM_PSRAM CACHE)
set(GAME_TFT_DMA_FROM_PSRAM OFF)
unset(GAME_TFT_PIPELINE CACHE)
set(GAME_TFT_PIPELINE OFF)
unset(GAME_TFT_CORE1 CACHE)
set(GAME_TFT_CORE1 ON)
option(GAME_ENABLE_PROFILING "Print frame timing summaries over USB serial" OFF)
unset(GAME_FRAMEBUFFER_IN_SRAM CACHE)
set(GAME_FRAMEBUFFER_IN_SRAM ON)
unset(GAME_ENABLE_DIRTY_RECT CACHE)
set(GAME_ENABLE_DIRTY_RECT ON)
unset(GAME_ENABLE_FRAME_DIFF CACHE)
set(GAME_ENABLE_FRAME_DIFF OFF)
unset(GAME_ENABLE_BACKGROUND_LAYER CACHE)
set(GAME_ENABLE_BACKGROUND_LAYER ON)
unset(GAME_OPTIMIZE_FOR_SPEED CACHE)
set(GAME_OPTIMIZE_FOR_SPEED ON)
option(GAME_BATTERY_WARNING "Monitor battery voltage on WeAct GP40" OFF)
if(GAME_BATTERY_WARNING AND NOT PICO_BOARD STREQUAL "weact_studio_rp2350b_core")
  message(FATAL_ERROR "Battery warning requires WeAct RP2350B Core")
endif()
option(GAME_AUDIO_VOLUME_KNOB "Read ADC2/GP42 as the audio master volume" OFF)
set(GAME_AUDIO_MAX_GAIN 100 CACHE STRING "Maximum audio output gain in percent (0..100)")
if(NOT GAME_AUDIO_MAX_GAIN MATCHES "^(0|[1-9][0-9]?|100)$")
  message(FATAL_ERROR "GAME_AUDIO_MAX_GAIN must be an integer from 0 to 100")
endif()
unset(GAME_AUDIO_I2S_CHAIN_DMA CACHE)
set(GAME_AUDIO_I2S_CHAIN_DMA ON)
unset(GAME_AUDIO_I2S_WIDE_MATH CACHE)
set(GAME_AUDIO_I2S_WIDE_MATH OFF)
option(GAME_AUDIO_I2S "Use MAX98357A I2S instead of PWM audio" OFF)
set(GAME_AUDIO_I2S_BCLK_PIN 26 CACHE STRING "I2S BCLK GPIO; WS uses the next GPIO")
set(GAME_AUDIO_I2S_DATA_PIN 28 CACHE STRING "I2S data GPIO")
if(GAME_AUDIO_I2S)
  if(NOT GAME_AUDIO_I2S_BCLK_PIN MATCHES "^(26|27)$" OR
     NOT GAME_AUDIO_I2S_DATA_PIN MATCHES "^(17|26|27|28)$")
    message(FATAL_ERROR "I2S must use free GPIOs 17 or 26..28; BCLK must be 26 or 27")
  endif()
  if(GAME_AUDIO_VOLUME_KNOB)
    if(PICO_BOARD STREQUAL "weact_studio_rp2350b_core")
      set(_game_audio_dial_data_pins "^(17|28)$")
    else()
      set(_game_audio_dial_data_pins "^17$")
    endif()
    if(NOT GAME_AUDIO_I2S_BCLK_PIN EQUAL 26 OR
       NOT GAME_AUDIO_I2S_DATA_PIN MATCHES "${_game_audio_dial_data_pins}")
      message(FATAL_ERROR "I2S with volume knob requires BCLK=26, WS=27, DATA=17 (WeAct also allows DATA=28)")
    endif()
  endif()
  math(EXPR GAME_AUDIO_I2S_WS_PIN "${GAME_AUDIO_I2S_BCLK_PIN} + 1")
  if(GAME_AUDIO_I2S_DATA_PIN EQUAL GAME_AUDIO_I2S_BCLK_PIN OR
     GAME_AUDIO_I2S_DATA_PIN EQUAL GAME_AUDIO_I2S_WS_PIN)
    message(FATAL_ERROR "I2S data must differ from BCLK and WS")
  endif()
endif()
unset(GAME_3D_LINE_CLIP CACHE)
set(GAME_3D_LINE_CLIP ON)
unset(GAME_3D_PROJECTION_CACHE CACHE)
set(GAME_3D_PROJECTION_CACHE ON)
unset(GAME_3D_DEPTH_SRAM CACHE)
set(GAME_3D_DEPTH_SRAM ON)
unset(GAME_3D_MODEL_SHARED CACHE)
set(GAME_3D_MODEL_SHARED ON)
unset(GAME_3D_TRIANGLE_MODE CACHE)
set(GAME_3D_TRIANGLE_MODE 2)
set(GAME_TFT_SPI_HZ 75000000 CACHE STRING "TFT SPI clock in Hz")
set(GAME_TFT_STRIPE_ROWS 32 CACHE STRING
  "Maximum rows copied to the SRAM TFT transfer stripe")
set(GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS 256 CACHE STRING
  "Extra pixel budget allowed when merging dirty rectangles")
set(GAME_SPRITE_CACHE_BYTES 65536 CACHE STRING
  "Internal SRAM bytes reserved for the fixed sprite cache")

if(NOT EXISTS "${PICORUBY_ROOT}/include/picoruby.h")
  message(FATAL_ERROR "PICORUBY_ROOT must point to a PicoRuby checkout")
endif()
if(NOT EXISTS "${PICORUBY_BUILD_DIR}/lib/libmruby.a")
  message(FATAL_ERROR "Missing ${PICORUBY_BUILD_DIR}/lib/libmruby.a. Build mruby_config.rb first.")
endif()
set(GAME_VM_RECEIPT "${PICORUBY_BUILD_DIR}/gaming-build.json")
if(NOT EXISTS "${GAME_VM_RECEIPT}")
  message(FATAL_ERROR "Missing VM build receipt. Build with tools/build/workflow.rb engine first.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GAME_VM_RECEIPT}")
file(READ "${GAME_VM_RECEIPT}" GAME_VM_SETTINGS)
string(JSON GAME_VM_PROFILE GET "${GAME_VM_SETTINGS}" vm_opt)
if(NOT GAME_VM_PROFILE MATCHES "^(size|speed)$")
  message(FATAL_ERROR "Unknown VM profile in ${GAME_VM_RECEIPT}")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(pico_ruby_gaming C CXX ASM)

pico_sdk_init()

set(FAT_ROOT "${CMAKE_CURRENT_LIST_DIR}/deps/picoruby-fatfs/mrbgems/picoruby-filesystem-fat"
  CACHE PATH "Pinned FatFs/SD port (separate from the PicoRuby runtime)")
if(NOT EXISTS "${FAT_ROOT}/lib/ff14b/source/ff.c")
  message(FATAL_ERROR "Missing FatFs source. Run ruby tools/build/workflow.rb setup")
endif()
set(MRUBY_INCLUDE "${PICORUBY_BUILD_DIR}/include")

set(PICORUBY_PLATFORM_SOURCES
  "${PICORUBY_ROOT}/mrbgems/picoruby-mruby/ports/rp2040/custom_ro_data.c"
  "${PICORUBY_ROOT}/mrbgems/picoruby-require/ports/rp2040/platform.c"
)

add_executable(pico_ruby_gaming
  src/main.c
  src/game_platform.c
  src/game_audio_dial.c
  src/fat_support.c
  src/mruby_task_hal.c
  ${PICORUBY_PLATFORM_SOURCES}
  "${FAT_ROOT}/lib/ff14b/source/ff.c"
  "${FAT_ROOT}/lib/ff14b/source/ffunicode.c"
  "${FAT_ROOT}/lib/ff14b/source/ffsystem.c"
  "${FAT_ROOT}/src/hal/diskio.c"
  "${FAT_ROOT}/src/hal/ram_disk.c"
  "${FAT_ROOT}/ports/rp2040/sd_disk.c"
)

target_compile_options(pico_ruby_gaming PRIVATE
  -Wall
  -Wextra
  -Wno-unused-parameter
  -ffunction-sections
  -fdata-sections
  -fshort-enums
  -mthumb
  $<$<CONFIG:Debug>:-O0;-g3>
  $<$<NOT:$<CONFIG:Debug>>:-Os>
)

if(GAME_OPTIMIZE_FOR_SPEED)
  set_source_files_properties(
    src/game_platform.c
    PROPERTIES COMPILE_OPTIONS "-O2;-fomit-frame-pointer"
  )
endif()

target_compile_definitions(pico_ruby_gaming PRIVATE
  GAME_BATTERY_WARNING=$<BOOL:${GAME_BATTERY_WARNING}>
  GAME_AUDIO_VOLUME_KNOB=$<BOOL:${GAME_AUDIO_VOLUME_KNOB}>
  GAME_AUDIO_MAX_GAIN=${GAME_AUDIO_MAX_GAIN}
  GAME_AUDIO_I2S_CHAIN_DMA=$<BOOL:${GAME_AUDIO_I2S_CHAIN_DMA}>
  GAME_AUDIO_I2S_WIDE_MATH=$<BOOL:${GAME_AUDIO_I2S_WIDE_MATH}>
  GAME_AUDIO_I2S=$<BOOL:${GAME_AUDIO_I2S}>
  GAME_AUDIO_I2S_BCLK_PIN=${GAME_AUDIO_I2S_BCLK_PIN}
  GAME_AUDIO_I2S_DATA_PIN=${GAME_AUDIO_I2S_DATA_PIN}
  GAME_VM_PROFILE="${GAME_VM_PROFILE}"
  PICO_RP2350=1
  GAME_TFT_SPI_HZ=${GAME_TFT_SPI_HZ}
  GAME_TFT_STRIPE_ROWS=${GAME_TFT_STRIPE_ROWS}
  GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS=${GAME_DIRTY_RECT_MERGE_EXTRA_PIXELS}
  GAME_SPRITE_CACHE_BYTES=${GAME_SPRITE_CACHE_BYTES}
  GAME_3D_LINE_CLIP=$<BOOL:${GAME_3D_LINE_CLIP}>
  GAME_3D_PROJECTION_CACHE=$<BOOL:${GAME_3D_PROJECTION_CACHE}>
  GAME_3D_DEPTH_SRAM=$<BOOL:${GAME_3D_DEPTH_SRAM}>
  GAME_3D_MODEL_SHARED=$<BOOL:${GAME_3D_MODEL_SHARED}>
  GAME_3D_TRIANGLE_MODE=${GAME_3D_TRIANGLE_MODE}
  PICORB_PLATFORM_RP2=1
  PICORB_VM_MRUBY=1
  MRB_USE_TASK_SCHEDULER=1
  PICORB_ALLOC_ESTALLOC
  PICORB_ALLOC_ALIGN=8
  MRB_INT64=1
  MRB_NO_BOXING=1
  MRB_CONSTRAINED_BASELINE_PROFILE=1
  ESTALLOC_ALIGNMENT=8
  MRB_32BIT=1
  MRB_USE_CUSTOM_RO_DATA_P=1
  MRB_LINK_TIME_RO_DATA_P=1
  USE_FAT_SD_DISK=1
  NO_CLOCK_GETTIME=1
)
if(GAME_TFT_DMA_FROM_PSRAM)
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_TFT_DMA_FROM_PSRAM=1)
endif()
if(GAME_TFT_PIPELINE)
  if(NOT GAME_ENABLE_DIRTY_RECT)
    message(FATAL_ERROR "GAME_TFT_PIPELINE requires GAME_ENABLE_DIRTY_RECT")
  endif()
  if(NOT GAME_FRAMEBUFFER_IN_SRAM)
    message(FATAL_ERROR "GAME_TFT_PIPELINE requires GAME_FRAMEBUFFER_IN_SRAM")
  endif()
  if(GAME_TFT_DMA_FROM_PSRAM)
    message(FATAL_ERROR "GAME_TFT_PIPELINE cannot use GAME_TFT_DMA_FROM_PSRAM")
  endif()
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_TFT_PIPELINE=1)
endif()
if(GAME_TFT_CORE1)
  if(NOT GAME_ENABLE_DIRTY_RECT)
    message(FATAL_ERROR "GAME_TFT_CORE1 requires GAME_ENABLE_DIRTY_RECT")
  endif()
  if(NOT GAME_ENABLE_BACKGROUND_LAYER)
    message(FATAL_ERROR "GAME_TFT_CORE1 requires GAME_ENABLE_BACKGROUND_LAYER")
  endif()
  if(NOT GAME_FRAMEBUFFER_IN_SRAM)
    message(FATAL_ERROR "GAME_TFT_CORE1 requires GAME_FRAMEBUFFER_IN_SRAM")
  endif()
  if(GAME_TFT_DMA_FROM_PSRAM)
    message(FATAL_ERROR "GAME_TFT_CORE1 cannot use GAME_TFT_DMA_FROM_PSRAM")
  endif()
  if(GAME_TFT_PIPELINE)
    message(FATAL_ERROR "GAME_TFT_CORE1 and GAME_TFT_PIPELINE are separate presenter modes")
  endif()
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_TFT_CORE1=1)
  target_link_libraries(pico_ruby_gaming PRIVATE pico_multicore)
endif()
if(GAME_ENABLE_PROFILING)
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_ENABLE_PROFILING=1)
endif()
if(GAME_FRAMEBUFFER_IN_SRAM)
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_FRAMEBUFFER_IN_SRAM=1)
endif()
if(GAME_ENABLE_DIRTY_RECT)
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_ENABLE_DIRTY_RECT=1)
endif()
if(GAME_ENABLE_FRAME_DIFF)
  if(NOT GAME_ENABLE_DIRTY_RECT)
    message(FATAL_ERROR "GAME_ENABLE_FRAME_DIFF requires GAME_ENABLE_DIRTY_RECT")
  endif()
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_ENABLE_FRAME_DIFF=1)
endif()
if(GAME_ENABLE_BACKGROUND_LAYER)
  if(NOT GAME_ENABLE_DIRTY_RECT)
    message(FATAL_ERROR "GAME_ENABLE_BACKGROUND_LAYER requires GAME_ENABLE_DIRTY_RECT")
  endif()
  if(GAME_ENABLE_FRAME_DIFF)
    message(FATAL_ERROR "GAME_ENABLE_BACKGROUND_LAYER and GAME_ENABLE_FRAME_DIFF are mutually exclusive")
  endif()
  target_compile_definitions(pico_ruby_gaming PRIVATE GAME_ENABLE_BACKGROUND_LAYER=1)
endif()

target_include_directories(pico_ruby_gaming PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${CMAKE_CURRENT_LIST_DIR}/mrbgems/picoruby-game/include"
  "${PICORUBY_ROOT}/include"
  "${PICORUBY_ROOT}/mrbgems/picoruby-mruby/include"
  "${PICORUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/include"
  "${PICORUBY_ROOT}/mrbgems/picoruby-require/include"
  "${PICORUBY_ROOT}/mrbgems/mruby-compiler/include"
  "${PICORUBY_ROOT}/mrbgems/mruby-compiler/lib/prism/include"
  "${PICORUBY_BUILD_DIR}/../prism/include"
  "${PICORUBY_ROOT}/mrbgems/picoruby-machine/include"
  "${PICORUBY_ROOT}/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-task/include"
  "${MRUBY_INCLUDE}"
  "${PICORUBY_BUILD_DIR}/mrbgems"
  "${FAT_ROOT}/include"
  "${FAT_ROOT}/lib/ff14b/source"
  "${FAT_ROOT}/ports/rp2040"
  "${FAT_ROOT}/src/hal"
)

target_link_directories(pico_ruby_gaming PRIVATE "${PICORUBY_BUILD_DIR}/lib")

if(GAME_AUDIO_VOLUME_KNOB OR GAME_BATTERY_WARNING)
  target_link_libraries(pico_ruby_gaming PRIVATE hardware_adc)
endif()

if(GAME_AUDIO_I2S)
  pico_generate_pio_header(pico_ruby_gaming "${CMAKE_CURRENT_LIST_DIR}/src/game_audio_i2s.pio")
  target_link_libraries(pico_ruby_gaming PRIVATE hardware_pio)
endif()

target_link_libraries(pico_ruby_gaming PRIVATE
  pico_stdlib
  m
  hardware_gpio
  hardware_spi
  hardware_dma
  hardware_pwm
  hardware_irq
  hardware_psram
  hardware_clocks
  hardware_timer
  pico_runtime
  mruby
)

pico_enable_stdio_usb(pico_ruby_gaming 1)
pico_enable_stdio_uart(pico_ruby_gaming 0)
pico_add_extra_outputs(pico_ruby_gaming)

# SDK 2.3 normally delegates UF2 creation to picotool. Keep a dependency-free
# fallback for offline builds (the normal picotool path remains unchanged).
if(PICO_NO_PICOTOOL)
  find_program(GAME_RUBY_EXECUTABLE ruby REQUIRED)
  set(GAME_UF2 "${CMAKE_CURRENT_BINARY_DIR}/pico_ruby_gaming.uf2")
  add_custom_command(
    OUTPUT "${GAME_UF2}"
    COMMAND "${GAME_RUBY_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/tools/build/bin_to_uf2.rb"
            "${CMAKE_CURRENT_BINARY_DIR}/pico_ruby_gaming.bin"
            "${GAME_UF2}"
    DEPENDS pico_ruby_gaming
            "${CMAKE_CURRENT_LIST_DIR}/tools/build/bin_to_uf2.rb"
    VERBATIM
  )
  add_custom_target(pico_ruby_gaming_uf2 ALL DEPENDS "${GAME_UF2}")
endif()

target_link_options(pico_ruby_gaming PRIVATE -Wl,--gc-sections)
# PicoRuby 4.x's interpreter uses over 3KB in one C frame. Reserve Core 0's
# 16KB stack at the top of regular SRAM; Core 1 keeps its physical scratch bank.
# The SDK's logical SCRATCH_Y region holds the Core 0 stack sections/symbols.
pico_set_linker_script_var(pico_ruby_gaming RAM_LENGTH 0x7c000)
pico_set_linker_script_var(pico_ruby_gaming SCRATCH_Y_ORIGIN 0x2007c000)
pico_set_linker_script_var(pico_ruby_gaming SCRATCH_Y_LENGTH 0x4000)
target_compile_definitions(pico_ruby_gaming PRIVATE
  PICO_STACK_SIZE=0x4000
  PICO_CORE1_STACK_SIZE=0x1000
  PICO_HEAP_SIZE=0x0400
)
