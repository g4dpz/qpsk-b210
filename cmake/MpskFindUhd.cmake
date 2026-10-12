# Locate UHD 4.x, preferring the install under MPSK_UHD_PREFIX.
#
# Why this exists: this project targets the Homebrew UHD install. A machine can
# also carry older UHD copies in /usr/local (headers and library), and the stock
# UHDConfig.cmake falls back to /usr/local through find_path/find_library PATHS.
# A mismatched header/library pair would build but misbehave at run time, so the
# result is checked instead of trusted.
#
# Sets (from UHDConfig.cmake): UHD_FOUND, UHD_VERSION, UHD_INCLUDE_DIRS,
# UHD_LIBRARIES, and provides the imported target UHD::uhd.

# UHDConfig.cmake calls find_dependency(Boost). Since CMake 3.30 the old FindBoost
# module is removed and policy CMP0167 selects Boost's own config files instead,
# which Homebrew's Boost installs. Setting it explicitly silences the developer
# warning that UHDConfig.cmake would otherwise trigger. The policy is only known
# to CMake 3.30 and newer, so it is guarded for older versions.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# UHDConfig.cmake gives $ENV{UHD_DIR} the highest priority for its search.
set(ENV{UHD_DIR} "${MPSK_UHD_PREFIX}")
list(PREPEND CMAKE_PREFIX_PATH "${MPSK_UHD_PREFIX}")

# First choice: the configured prefix only.
# The prefix itself is a hint so CMake tries its standard install layouts under it,
# which covers lib/cmake/uhd (Homebrew) and lib/<arch>/cmake/uhd (Debian, Ubuntu).
find_package(UHD 4.0 CONFIG QUIET
  HINTS "${MPSK_UHD_PREFIX}" "${MPSK_UHD_PREFIX}/lib/cmake/uhd"
  NO_DEFAULT_PATH)

if(UHD_FOUND)
  # UHDConfig.cmake still searches /usr/local as a fallback, so confirm that
  # the headers and library really came from the requested prefix.
  get_filename_component(_mpsk_prefix "${MPSK_UHD_PREFIX}" REALPATH)
  set(_mpsk_uhd_paths "${UHD_LIBRARIES}")
  list(APPEND _mpsk_uhd_paths ${UHD_INCLUDE_DIRS})
  foreach(_p IN LISTS _mpsk_uhd_paths)
    get_filename_component(_real "${_p}" REALPATH)
    string(FIND "${_real}" "${_mpsk_prefix}/" _pos)
    if(NOT _pos EQUAL 0)
      message(FATAL_ERROR
        "UHD was found under ${MPSK_UHD_PREFIX}, but ${_p} resolves outside it.\n"
        "A different UHD install is shadowing the intended one (for example an "
        "older copy in /usr/local). Remove it, fix PKG_CONFIG_PATH, or set "
        "-DMPSK_UHD_PREFIX=<the prefix you want>.")
    endif()
  endforeach()
else()
  message(WARNING
    "UHD 4.x was not found under ${MPSK_UHD_PREFIX}. Falling back to the default "
    "search paths; the result may be an older install.")
  find_package(UHD 4.0 CONFIG REQUIRED)
endif()

# Newer UHD packages (Homebrew's 4.10) export the imported target UHD::uhd. Older
# ones (Ubuntu 24.04's 4.6) only set UHD_LIBRARIES and UHD_INCLUDE_DIRS, so build
# an equivalent target from those. Either way the rest of the project links
# UHD::uhd and does not need to know which kind of package it got.
if(NOT TARGET UHD::uhd)
  if(NOT UHD_LIBRARIES OR NOT UHD_INCLUDE_DIRS)
    message(FATAL_ERROR
      "UHD was found, but it provides neither the target UHD::uhd nor the "
      "variables UHD_LIBRARIES and UHD_INCLUDE_DIRS")
  endif()
  add_library(UHD::uhd INTERFACE IMPORTED)
  set_target_properties(UHD::uhd PROPERTIES
    INTERFACE_LINK_LIBRARIES "${UHD_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${UHD_INCLUDE_DIRS}")
  message(STATUS "UHD::uhd built from UHD_LIBRARIES and UHD_INCLUDE_DIRS "
                 "(this UHD package does not export the target)")
endif()
