cmake_minimum_required(VERSION 3.16)

project(mpsk_b200mini
  VERSION 0.1.0
  DESCRIPTION "M-PSK (QPSK, 8-PSK) transceiver for the Ettus USRP B200-mini using UHD"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language and build type
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MPSK_WITH_UHD "Build the UHD radio layer and the hardware subcommands" ON)
option(MPSK_BUILD_TESTS "Build the unit and integration tests" ON)
set(MPSK_UHD_PREFIX "/opt/homebrew" CACHE PATH
  "Install prefix searched first for UHD (the Homebrew prefix on Apple Silicon). \
Older copies in /usr/local are deliberately not preferred.")

# Warnings are applied per target through this interface library, so that
# third-party headers and imported targets are never affected.
add_library(mpsk_warnings INTERFACE)
target_compile_options(mpsk_warnings INTERFACE -Wall -Wextra)

# ---------------------------------------------------------------------------
# UHD discovery (only when the radio layer is requested)
# ---------------------------------------------------------------------------
if(MPSK_WITH_UHD)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  include(MpskFindUhd)
else()
  message(STATUS "MPSK_WITH_UHD=OFF: building DSP, simulation and link layers only")
endif()

# ---------------------------------------------------------------------------
# Libraries and application
# ---------------------------------------------------------------------------
add_subdirectory(src/dsp)
add_subdirectory(src/io)
add_subdirectory(src/sim)
add_subdirectory(src/link)
if(MPSK_WITH_UHD)
  add_subdirectory(src/radio)
endif()
add_subdirectory(src/app)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MPSK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "---- mpsk configuration ----")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  UHD radio  : ${MPSK_WITH_UHD}")
if(MPSK_WITH_UHD)
  message(STATUS "  UHD version: ${UHD_VERSION}")
  message(STATUS "  UHD headers: ${UHD_INCLUDE_DIRS}")
  message(STATUS "  UHD library: ${UHD_LIBRARIES}")
endif()
message(STATUS "  Tests      : ${MPSK_BUILD_TESTS}")
