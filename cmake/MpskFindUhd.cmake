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
find_package(UHD 4.0 CONFIG QUIET
  HINTS "${MPSK_UHD_PREFIX}/lib/cmake/uhd"
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

if(NOT TARGET UHD::uhd)
  message(FATAL_ERROR "UHD was found but the imported target UHD::uhd is missing")
endif()
