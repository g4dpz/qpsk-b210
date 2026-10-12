# Channel model and simulated radio. Depends only on the DSP library.
add_library(mpsk_sim STATIC
  sim_info.cpp)

target_include_directories(mpsk_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mpsk_sim PUBLIC mpsk_dsp PRIVATE mpsk_warnings)
