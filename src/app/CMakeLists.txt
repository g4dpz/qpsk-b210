# The mpsk executable. Subcommands are added in later tasks.
add_executable(mpsk main.cpp)

# Library dependencies arrive transitively (mpsk_radio -> mpsk_link -> mpsk_dsp),
# so each is named only once. Naming a library both directly and transitively
# makes the linker see it twice and warn about duplicate libraries.
target_link_libraries(mpsk PRIVATE mpsk_sim mpsk_warnings)

if(MPSK_WITH_UHD)
  target_link_libraries(mpsk PRIVATE mpsk_radio)
  target_compile_definitions(mpsk PRIVATE MPSK_WITH_UHD=1)
else()
  target_link_libraries(mpsk PRIVATE mpsk_link)
endif()
