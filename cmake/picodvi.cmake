set(PICODVI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../deps/picodvi" CACHE PATH "Pinned PicoDVI checkout")
set(_libdvi "${PICODVI_ROOT}/software/libdvi")
set(_serialiser "${_libdvi}/dvi_serialiser.pio")
if(NOT EXISTS "${_serialiser}")
  message(FATAL_ERROR "Missing PicoDVI. Run ruby tools/build/workflow.rb setup")
endif()

# PicoDVI's PIO initializer uses 32-bit pin masks. Preserve the serialiser
# program and widen only its two pin-mask calls for RP2350B GPIO32..37.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_serialiser}")
file(READ "${_serialiser}" _pio_source)
string(REPLACE
  "pio_sm_set_pins_with_mask(pio, sm, 2u << data_pins, 3u << data_pins);"
  "pio_sm_set_pins_with_mask64(pio, sm, 2ull << data_pins, 3ull << data_pins);"
  _pio_source "${_pio_source}")
string(REPLACE
  "pio_sm_set_pindirs_with_mask(pio, sm, ~0u, 3u << data_pins);"
  "pio_sm_set_pindirs_with_mask64(pio, sm, ~0ull, 3ull << data_pins);"
  _pio_source "${_pio_source}")
if(NOT _pio_source MATCHES "pio_sm_set_pins_with_mask64" OR
   NOT _pio_source MATCHES "pio_sm_set_pindirs_with_mask64" OR
   _pio_source MATCHES "pio_sm_set_pin(dirs|s)_with_mask[(]")
  message(FATAL_ERROR "Unexpected PicoDVI serialiser pin setup; check the pinned revision")
endif()
set(_pio_copy "${CMAKE_CURRENT_BINARY_DIR}/picodvi/dvi_serialiser.pio")
file(CONFIGURE OUTPUT "${_pio_copy}" CONTENT "${_pio_source}" @ONLY)

add_library(game_picodvi INTERFACE)
target_sources(game_picodvi INTERFACE
  "${_libdvi}/dvi.c"
  "${_libdvi}/dvi_serialiser.c"
  "${_libdvi}/dvi_timing.c"
  "${_libdvi}/tmds_encode.c"
  "${_libdvi}/tmds_encode.S"
)
target_include_directories(game_picodvi INTERFACE "${_libdvi}")
target_compile_definitions(game_picodvi INTERFACE DVI_N_TMDS_BUFFERS=0)
target_link_libraries(game_picodvi INTERFACE hardware_dma hardware_interp hardware_pio hardware_pwm pico_util)
pico_generate_pio_header(game_picodvi "${_pio_copy}")
pico_generate_pio_header(game_picodvi "${_libdvi}/tmds_encode_1bpp.pio")
