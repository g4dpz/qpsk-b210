# --------------------------------------------------------------------------
# Test targets for qpsk-b200-codec
# --------------------------------------------------------------------------

include(GoogleTest)

# --------------------------------------------------------------------------
# Helper function: add a test executable linked to common dependencies
# --------------------------------------------------------------------------
function(qpsk_add_test TARGET_NAME)
    # Remaining arguments are source files
    add_executable(${TARGET_NAME} ${ARGN})
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            qpsk_b200
            GTest::gtest
            GTest::gtest_main
    )
    target_include_directories(${TARGET_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )
    gtest_discover_tests(${TARGET_NAME})
endfunction()

# --------------------------------------------------------------------------
# Helper function: add a property-based test (links RapidCheck + gtest)
# --------------------------------------------------------------------------
function(qpsk_add_property_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            qpsk_b200
            GTest::gtest
            GTest::gtest_main
            rapidcheck
            rapidcheck_gtest
    )
    target_include_directories(${TARGET_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )
    gtest_discover_tests(${TARGET_NAME})
endfunction()

# --------------------------------------------------------------------------
# Unit tests
# --------------------------------------------------------------------------
qpsk_add_test(test_types unit/test_types.cpp)
qpsk_add_test(test_config_validate unit/test_config_validate.cpp)
qpsk_add_test(test_config_json unit/test_config_json.cpp)
qpsk_add_test(test_symbol_mapper unit/test_symbol_mapper.cpp)
qpsk_add_test(test_pulse_shaper unit/test_pulse_shaper.cpp)
qpsk_add_test(test_fec unit/test_fec.cpp)
qpsk_add_test(test_frame unit/test_frame.cpp)
qpsk_add_test(test_carrier_sync unit/test_carrier_sync.cpp)
qpsk_add_test(test_timing_recovery unit/test_timing_recovery.cpp)
qpsk_add_test(test_b200_interface unit/test_b200_interface.cpp)
qpsk_add_test(test_tcp_servers unit/test_tcp_servers.cpp)
qpsk_add_test(test_encoder_decoder unit/test_encoder_decoder.cpp)
qpsk_add_test(test_spsc_queue unit/test_spsc_queue.cpp)

# --------------------------------------------------------------------------
# Property-based tests
# --------------------------------------------------------------------------
qpsk_add_property_test(test_config_props property/test_config_props.cpp)
qpsk_add_property_test(test_symbol_mapper_props property/test_symbol_mapper_props.cpp)
qpsk_add_property_test(test_pulse_shaper_props property/test_pulse_shaper_props.cpp)
qpsk_add_property_test(test_fec_props property/test_fec_props.cpp)
qpsk_add_property_test(test_frame_props property/test_frame_props.cpp)
qpsk_add_property_test(test_carrier_sync_props property/test_carrier_sync_props.cpp)
qpsk_add_property_test(test_timing_recovery_props property/test_timing_recovery_props.cpp)
qpsk_add_property_test(test_tcp_props property/test_tcp_props.cpp)
qpsk_add_property_test(test_codec_roundtrip_props property/test_codec_roundtrip_props.cpp)

# --------------------------------------------------------------------------
# Integration tests
# --------------------------------------------------------------------------
qpsk_add_test(test_b200_config integration/test_b200_config.cpp)
qpsk_add_test(test_tcp_multi_client integration/test_tcp_multi_client.cpp)
qpsk_add_test(test_ber_performance integration/test_ber_performance.cpp)
qpsk_add_test(test_full_pipeline integration/test_full_pipeline.cpp)
