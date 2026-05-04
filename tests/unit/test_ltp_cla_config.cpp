#include "ltp_cla/config.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <string>

using namespace ltp;

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, Defaults) {
    auto cfg = LtpClaConfig::defaults();
    EXPECT_EQ(cfg.ltp.local_engine_id, 1u);
    EXPECT_EQ(cfg.ltp.remote_engine_id, 2u);
    EXPECT_EQ(cfg.ltp.max_segment_size, 1400u);
    EXPECT_EQ(cfg.ltp.retransmission_timeout_ms, 30000u);
    EXPECT_EQ(cfg.ltp.max_retransmissions, 10u);
    EXPECT_EQ(cfg.ltp.max_concurrent_sessions, 100u);
    EXPECT_EQ(cfg.cla.tx_addr, "127.0.0.1");
    EXPECT_EQ(cfg.cla.tx_port, 5000);
    EXPECT_EQ(cfg.cla.rx_port, 5001);
    EXPECT_EQ(cfg.ingress.port, 4556);
    EXPECT_TRUE(cfg.ingress.default_reliable);
    EXPECT_EQ(cfg.egress.port, 4557);
    EXPECT_EQ(cfg.egress.max_buffer_bytes, 10485760u);
    EXPECT_EQ(cfg.log_level, "info");
}

// ---------------------------------------------------------------------------
// JSON round-trip
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, JsonRoundTrip) {
    auto cfg = LtpClaConfig::defaults();
    cfg.ltp.local_engine_id = 42;
    cfg.ltp.max_segment_size = 800;
    cfg.cla.tx_port = 6000;
    cfg.ingress.default_reliable = false;
    cfg.egress.max_buffer_bytes = 5000000;
    cfg.log_level = "debug";

    auto json_str = cfg.to_json();
    auto loaded = LtpClaConfig::from_json(json_str);

    EXPECT_EQ(cfg, loaded);
}

// ---------------------------------------------------------------------------
// JSON file round-trip
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, JsonFileRoundTrip) {
    auto cfg = LtpClaConfig::defaults();
    cfg.ltp.local_engine_id = 99;

    std::string path = "/tmp/ltp_cla_test_config.json";
    cfg.to_json_file(path);

    auto loaded = LtpClaConfig::from_json_file(path);
    EXPECT_EQ(cfg, loaded);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Partial JSON (missing fields use defaults)
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, PartialJson) {
    std::string json_str = R"({"ltp":{"local_engine_id":77}})";
    auto cfg = LtpClaConfig::from_json(json_str);

    EXPECT_EQ(cfg.ltp.local_engine_id, 77u);
    // Everything else should be default
    EXPECT_EQ(cfg.ltp.max_segment_size, 1400u);
    EXPECT_EQ(cfg.cla.tx_port, 5000);
    EXPECT_EQ(cfg.log_level, "info");
}

// ---------------------------------------------------------------------------
// CLI overrides
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, CliOverrides) {
    auto cfg = LtpClaConfig::defaults();

    const char* argv[] = {
        "ltp_cla",
        "--ltp.local_engine_id=55",
        "--ltp.max_segment_size=500",
        "--cla.tx_port=7000",
        "--ingress.default_reliable=false",
        "--log_level=debug",
    };
    int argc = 6;

    cfg.apply_cli_overrides(argc, argv);

    EXPECT_EQ(cfg.ltp.local_engine_id, 55u);
    EXPECT_EQ(cfg.ltp.max_segment_size, 500u);
    EXPECT_EQ(cfg.cla.tx_port, 7000);
    EXPECT_FALSE(cfg.ingress.default_reliable);
    EXPECT_EQ(cfg.log_level, "debug");
}

// ---------------------------------------------------------------------------
// CLI overrides take precedence over JSON
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, CliOverridesJson) {
    std::string json_str = R"({"ltp":{"local_engine_id":10,"max_segment_size":800}})";
    auto cfg = LtpClaConfig::from_json(json_str);

    const char* argv[] = {"ltp_cla", "--ltp.local_engine_id=20"};
    cfg.apply_cli_overrides(2, argv);

    EXPECT_EQ(cfg.ltp.local_engine_id, 20u);  // CLI wins
    EXPECT_EQ(cfg.ltp.max_segment_size, 800u); // JSON preserved
}

// ---------------------------------------------------------------------------
// Validation — valid config
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, ValidConfig) {
    auto cfg = LtpClaConfig::defaults();
    auto errors = cfg.validate();
    EXPECT_TRUE(errors.empty());
}

// ---------------------------------------------------------------------------
// Validation — invalid parameters
// ---------------------------------------------------------------------------

TEST(LtpClaConfigTest, InvalidEngineId) {
    auto cfg = LtpClaConfig::defaults();
    cfg.ltp.local_engine_id = 0;
    auto errors = cfg.validate();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("local_engine_id"), std::string::npos);
}

TEST(LtpClaConfigTest, InvalidMaxSegmentSize) {
    auto cfg = LtpClaConfig::defaults();
    cfg.ltp.max_segment_size = 0;
    auto errors = cfg.validate();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("max_segment_size"), std::string::npos);
}

TEST(LtpClaConfigTest, InvalidTimeout) {
    auto cfg = LtpClaConfig::defaults();
    cfg.ltp.retransmission_timeout_ms = 50;
    auto errors = cfg.validate();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("retransmission_timeout_ms"), std::string::npos);
}

TEST(LtpClaConfigTest, InvalidLogLevel) {
    auto cfg = LtpClaConfig::defaults();
    cfg.log_level = "verbose";
    auto errors = cfg.validate();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("log_level"), std::string::npos);
}

TEST(LtpClaConfigTest, InvalidBufferSize) {
    auto cfg = LtpClaConfig::defaults();
    cfg.egress.max_buffer_bytes = 0;
    auto errors = cfg.validate();
    ASSERT_FALSE(errors.empty());
    EXPECT_NE(errors[0].find("max_buffer_bytes"), std::string::npos);
}

TEST(LtpClaConfigTest, MultipleErrors) {
    auto cfg = LtpClaConfig::defaults();
    cfg.ltp.local_engine_id = 0;
    cfg.ltp.max_segment_size = 0;
    cfg.log_level = "bad";
    auto errors = cfg.validate();
    EXPECT_GE(errors.size(), 3u);
}
