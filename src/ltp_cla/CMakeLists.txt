# --------------------------------------------------------------------------
# ltp_cla library
# --------------------------------------------------------------------------

# Collect all .cpp source files in src/ltp_cla/ EXCEPT main.cpp
file(GLOB_RECURSE LTP_CLA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER LTP_CLA_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# When there are no source files yet, create an INTERFACE library so the
# project still configures cleanly.  Once the first .cpp is added, switch
# to a STATIC library.
if(LTP_CLA_SOURCES)
    add_library(ltp_cla STATIC ${LTP_CLA_SOURCES})
else()
    add_library(ltp_cla INTERFACE)
endif()

# Public include path — available to this library and all consumers
if(LTP_CLA_SOURCES)
    target_include_directories(ltp_cla
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(ltp_cla
        PUBLIC
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
else()
    target_include_directories(ltp_cla
        INTERFACE
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(ltp_cla
        INTERFACE
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
endif()

# --------------------------------------------------------------------------
# ltp_cla_app executable (main entry point)
# --------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
    add_executable(ltp_cla_app main.cpp)
    target_link_libraries(ltp_cla_app
        PRIVATE
            ltp_cla
            Threads::Threads
    )
    target_include_directories(ltp_cla_app
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )
endif()
