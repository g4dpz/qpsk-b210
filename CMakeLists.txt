cmake_minimum_required(VERSION 3.14)
project(qpsk-b200-codec
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(QPSK_ENABLE_UHD    "Build with UHD (Ettus USRP Hardware Driver) support" ON)
option(QPSK_BUILD_TESTS   "Build unit, property, and integration tests"         ON)

# --------------------------------------------------------------------------
# Dependencies — system packages
# --------------------------------------------------------------------------
find_package(Threads REQUIRED)

if(QPSK_ENABLE_UHD)
    find_package(UHD QUIET)
    if(NOT UHD_FOUND)
        message(WARNING "UHD not found — B200 hardware support disabled. "
                        "Install UHD or set -DQPSK_ENABLE_UHD=OFF to suppress this warning.")
        set(QPSK_ENABLE_UHD OFF)
    endif()
endif()

if(QPSK_ENABLE_UHD)
    add_compile_definitions(QPSK_HAS_UHD)
endif()

# --------------------------------------------------------------------------
# Dependencies — fetched via FetchContent
# --------------------------------------------------------------------------
include(FetchContent)

# nlohmann/json (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# spdlog (header-only mode)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)

FetchContent_MakeAvailable(nlohmann_json spdlog)

# --------------------------------------------------------------------------
# Test dependencies (only when tests are enabled)
# --------------------------------------------------------------------------
if(QPSK_BUILD_TESTS)
    enable_testing()
    # Run tests sequentially — TCP tests use loopback ports that conflict in parallel
    set(CTEST_PARALLEL_LEVEL 1 CACHE STRING "CTest parallel level")

    # Google Test
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.16.0
    )
    # Prevent GoogleTest from overriding compiler/linker settings on Windows
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    # RapidCheck
    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG        master
    )
    # Enable RapidCheck's gtest integration
    set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest rapidcheck)
endif()

# --------------------------------------------------------------------------
# Project source and include directories
# --------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(src/ltp_cla)

if(QPSK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
