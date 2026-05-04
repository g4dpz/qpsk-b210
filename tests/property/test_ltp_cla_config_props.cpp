// Feature: bp-ltp-dtn, Property 10: Configuration Serialization Round-Trip
// Feature: bp-ltp-dtn, Property 18: Invalid Configuration Rejection

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "ltp_cla/config.h"

using namespace ltp;

// ---------------------------------------------------------------------------
// Generator: valid LtpClaConfig
// ---------------------------------------------------------------------------

static rc::Gen<LtpClaConfig> genValidConfig() {
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(1, 10000),   // local_engine_id
            rc::gen::inRange<uint64_t>(1, 10000),   // remote_engine_id
            rc::gen::inRange<uint32_t>(1, 65535),    // max_segment_size
            rc::gen::inRange<uint32_t>(100, 60000),  // retransmission_timeout_ms
            rc::gen::inRange<uint8_t>(1, 50),        // max_retransmissions
            rc::gen::inRange<uint32_t>(1, 1000),     // max_concurrent_sessions
            rc::gen::inRange<uint16_t>(1, 65535),    // cla tx_port
            rc::gen::inRange<uint16_t>(1, 65535),    // cla rx_port
            rc::gen::inRange<uint16_t>(0, 65535),    // ingress port
            rc::gen::inRange<uint16_t>(0, 65535),    // egress port
            rc::gen::arbitrary<bool>(),              // default_reliable
            rc::gen::inRange<uint64_t>(1, 100000000) // max_buffer_bytes
        ),
        [](const auto& t) {
            LtpClaConfig cfg;
            cfg.ltp.local_engine_id = std::get<0>(t);
            cfg.ltp.remote_engine_id = std::get<1>(t);
            cfg.ltp.max_segment_size = std::get<2>(t);
            cfg.ltp.retransmission_timeout_ms = std::get<3>(t);
            cfg.ltp.max_retransmissions = std::get<4>(t);
            cfg.ltp.max_concurrent_sessions = std::get<5>(t);
            cfg.cla.tx_port = std::get<6>(t);
            cfg.cla.rx_port = std::get<7>(t);
            cfg.ingress.port = std::get<8>(t);
            cfg.egress.port = std::get<9>(t);
            cfg.ingress.default_reliable = std::get<10>(t);
            cfg.egress.max_buffer_bytes = std::get<11>(t);
            cfg.log_level = "info";
            return cfg;
        });
}

// ---------------------------------------------------------------------------
// Property 10: Config Serialization Round-Trip
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpClaConfigProperty10, JsonRoundTrip, ()) {
    auto cfg = *genValidConfig();

    auto json_str = cfg.to_json();
    auto loaded = LtpClaConfig::from_json(json_str);

    RC_ASSERT(cfg == loaded);
}

// ---------------------------------------------------------------------------
// Property 18: Invalid Configuration Rejection
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpClaConfigProperty18, InvalidConfigRejected, ()) {
    auto cfg = *genValidConfig();

    // Corrupt one parameter to make it invalid
    auto which = *rc::gen::inRange(0, 5);
    switch (which) {
        case 0: cfg.ltp.local_engine_id = 0; break;
        case 1: cfg.ltp.max_segment_size = 0; break;
        case 2: cfg.ltp.retransmission_timeout_ms = *rc::gen::inRange<uint32_t>(0, 99); break;
        case 3: cfg.egress.max_buffer_bytes = 0; break;
        case 4: cfg.log_level = "invalid_level"; break;
    }

    auto errors = cfg.validate();
    RC_ASSERT(!errors.empty());
}
