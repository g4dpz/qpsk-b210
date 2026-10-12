# One test executable per area, each registered with CTest. Add a new test with
#   mpsk_add_test(<name> <source.cpp> [NO_DEFAULT_LIBS] [LIBS <libs>...])
# By default the simulation and link libraries (and through them the DSP
# library) are linked into every test. A library named both directly and
# transitively makes the linker warn about duplicates, so a test that needs
# mpsk_radio (which already brings in mpsk_link and mpsk_dsp) passes
# NO_DEFAULT_LIBS and lists what it needs.

function(mpsk_add_test name source)
  cmake_parse_arguments(ARG "NO_DEFAULT_LIBS" "" "LIBS" ${ARGN})
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE mpsk_warnings ${ARG_LIBS})
  if(NOT ARG_NO_DEFAULT_LIBS)
    target_link_libraries(${name} PRIVATE mpsk_sim mpsk_link)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

mpsk_add_test(test_scaffold test_scaffold.cpp)
mpsk_add_test(test_constellation test_constellation.cpp)
mpsk_add_test(test_bits_crc test_bits_crc.cpp)
mpsk_add_test(test_prbs_scrambler test_prbs_scrambler.cpp)
mpsk_add_test(test_rrc test_rrc.cpp)
mpsk_add_test(test_fir test_fir.cpp)
mpsk_add_test(test_pulse_shaping test_pulse_shaping.cpp)
mpsk_add_test(test_preamble test_preamble.cpp)
mpsk_add_test(test_framer test_framer.cpp)
mpsk_add_test(test_modulator test_modulator.cpp)
mpsk_add_test(test_io test_io.cpp LIBS mpsk_io)

if(MPSK_WITH_UHD)
  mpsk_add_test(test_uhd_consistency test_uhd_consistency.cpp
                NO_DEFAULT_LIBS LIBS mpsk_radio)
endif()
mpsk_add_test(test_dc_blocker test_dc_blocker.cpp)
mpsk_add_test(test_channel_ops test_channel_ops.cpp)
mpsk_add_test(test_rx_frontend test_rx_frontend.cpp)
mpsk_add_test(test_preamble_detector test_preamble_detector.cpp)
mpsk_add_test(test_farrow test_farrow.cpp)
mpsk_add_test(test_loops test_loops.cpp)
mpsk_add_test(test_metrics test_metrics.cpp)
mpsk_add_test(test_demodulator test_demodulator.cpp)
mpsk_add_test(test_demodulator_noise test_demodulator_noise.cpp)
mpsk_add_test(test_channel test_channel.cpp)
mpsk_add_test(test_receiver test_receiver.cpp)
mpsk_add_test(test_loopback test_loopback.cpp)
mpsk_add_test(test_sweep test_sweep.cpp)

# The command line: the parser and the subcommands, run in-process.
mpsk_add_test(test_option_parser test_option_parser.cpp NO_DEFAULT_LIBS LIBS mpsk_app)
mpsk_add_test(test_cli test_cli.cpp NO_DEFAULT_LIBS LIBS mpsk_app mpsk_io)
