# DSP blocks. No I/O and no UHD, so every block can be unit-tested offline.
add_library(mpsk_dsp STATIC
  bits.cpp
  constellation.cpp
  crc.cpp
  fir.cpp
  framer.cpp
  lfsr.cpp
  modulator.cpp
  preamble.cpp
  prbs.cpp
  rrc.cpp
  scrambler.cpp
  version.cpp)

target_include_directories(mpsk_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mpsk_dsp PRIVATE mpsk_warnings)
target_compile_definitions(mpsk_dsp PUBLIC MPSK_VERSION_STRING="${PROJECT_VERSION}")
