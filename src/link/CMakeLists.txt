# Echo protocol and the radio interface. Reaches hardware only through IRadio,
# so it never includes UHD headers.
add_library(mpsk_link STATIC
  link_info.cpp)

target_include_directories(mpsk_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mpsk_link PUBLIC mpsk_dsp PRIVATE mpsk_warnings)
