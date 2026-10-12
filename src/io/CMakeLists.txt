# File and stream I/O: baseband sample files and payload sources. Kept out of
# mpsk_dsp so that library stays free of any I/O. No UHD here either.
add_library(mpsk_io STATIC
  payload_source.cpp
  sample_file.cpp)

target_include_directories(mpsk_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mpsk_io PUBLIC mpsk_dsp PRIVATE mpsk_warnings)
