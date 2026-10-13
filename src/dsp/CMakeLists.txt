# DSP blocks. No I/O and no UHD, so every block can be unit-tested offline.
add_library(mpsk_dsp STATIC
  bits.cpp
  carrier_loop.cpp
  constellation.cpp
  crc.cpp
  dc_blocker.cpp
  demodulator.cpp
  farrow.cpp
  fir.cpp
  framer.cpp
  interpolator.cpp
  lfsr.cpp
  loop_gains.cpp
  metrics.cpp
  modulator.cpp
  preamble.cpp
  preamble_detector.cpp
  prbs.cpp
  rrc.cpp
  receiver.cpp
  rx_frontend.cpp
  scrambler.cpp
  spectrum.cpp
  timing_loop.cpp
  version.cpp)

target_include_directories(mpsk_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mpsk_dsp PRIVATE mpsk_warnings)
target_compile_definitions(mpsk_dsp PUBLIC MPSK_VERSION_STRING="${PROJECT_VERSION}")
