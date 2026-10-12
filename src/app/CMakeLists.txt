# The command line. The subcommands and the option parser are in a library so the
# tests can run them in-process and read what they print; main.cpp only passes
# the arguments in.
add_library(mpsk_app STATIC
  cli.cpp
  cmd_loopback.cpp
  common_options.cpp
  option_parser.cpp)
target_include_directories(mpsk_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Library dependencies arrive transitively (mpsk_radio -> mpsk_link -> mpsk_dsp), so
# each is named only once. Naming a library both directly and transitively makes
# the linker see it twice and warn about duplicate libraries.
target_link_libraries(mpsk_app PUBLIC mpsk_sim mpsk_io PRIVATE mpsk_warnings)
if(MPSK_WITH_UHD)
  target_link_libraries(mpsk_app PUBLIC mpsk_radio)
  target_compile_definitions(mpsk_app PRIVATE MPSK_WITH_UHD=1)
else()
  target_link_libraries(mpsk_app PUBLIC mpsk_link)
endif()

# The mpsk executable.
add_executable(mpsk main.cpp)
target_link_libraries(mpsk PRIVATE mpsk_app mpsk_warnings)
