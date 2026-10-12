# UHD radio layer. The only place UHD headers are included. Built only when
# MPSK_WITH_UHD is ON.
add_library(mpsk_radio STATIC
  uhd_info.cpp)

target_include_directories(mpsk_radio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mpsk_radio PUBLIC mpsk_link UHD::uhd PRIVATE mpsk_warnings)

# By default CMake passes the include directories of an imported target such as
# UHD::uhd as -isystem. Apple clang then searches /usr/local/include BEFORE an
# -isystem /opt/homebrew/include, so an older UHD left in /usr/local supplies
# the headers while the Homebrew library is linked. That mismatch builds fine
# and misbehaves at run time. Passing the directory as a plain -I makes the
# Homebrew headers win. Keep UHD headers out of this layer's public headers so
# this setting only needs to apply here.
set_target_properties(mpsk_radio PROPERTIES NO_SYSTEM_FROM_IMPORTED ON)
