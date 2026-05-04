# --------------------------------------------------------------------------
# qpsk_b200 library
# --------------------------------------------------------------------------

# Collect all .cpp source files in src/ EXCEPT main.cpp
file(GLOB_RECURSE QPSK_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER QPSK_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# When there are no source files yet, create an INTERFACE library so the
# project still configures cleanly.  Once the first .cpp is added, switch
# to a STATIC library.
if(QPSK_SOURCES)
    add_library(qpsk_b200 STATIC ${QPSK_SOURCES})
else()
    add_library(qpsk_b200 INTERFACE)
endif()

# Public include path — available to this library and all consumers
if(QPSK_SOURCES)
    target_include_directories(qpsk_b200
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(qpsk_b200
        PUBLIC
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
    if(QPSK_ENABLE_UHD)
        target_link_libraries(qpsk_b200 PUBLIC ${UHD_LIBRARIES})
        target_include_directories(qpsk_b200 PUBLIC ${UHD_INCLUDE_DIRS})
    endif()
else()
    target_include_directories(qpsk_b200
        INTERFACE
            ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(qpsk_b200
        INTERFACE
            nlohmann_json::nlohmann_json
            spdlog::spdlog
    )
    if(QPSK_ENABLE_UHD)
        target_link_libraries(qpsk_b200 INTERFACE ${UHD_LIBRARIES})
        target_include_directories(qpsk_b200 INTERFACE ${UHD_INCLUDE_DIRS})
    endif()
endif()

# --------------------------------------------------------------------------
# qpsk_b200_app executable (main entry point)
# --------------------------------------------------------------------------
add_executable(qpsk_b200_app main.cpp)
target_link_libraries(qpsk_b200_app
    PRIVATE
        qpsk_b200
        Threads::Threads
)
target_include_directories(qpsk_b200_app
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
