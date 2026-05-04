#include <gtest/gtest.h>
#include "qpsk_b200/types.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Helper: create a temporary file path for test JSON files
// ---------------------------------------------------------------------------
static std::string temp_json_path(const std::string& suffix) {
    return "/tmp/test_config_json_" + suffix + ".json";
}

// Helper: clean up a temp file
static void remove_file(const std::string& path) {
    std::remove(path.c_str());
}

// Helper: write raw string to a file
static void write_raw(const std::string& path, const std::string& content) {
    std::ofstream ofs(path);
    ASSERT_TRUE(ofs.is_open()) << "Failed to create test file: " << path;
    ofs << content;
}

// ---------------------------------------------------------------------------
// Round-trip: to_json then from_json produces equivalent Config
// ---------------------------------------------------------------------------

TEST(ConfigJson, RoundTripDefaults) {
    auto path = temp_json_path("roundtrip_defaults");
    auto original = Config::defaults();

    original.to_json(path);
    auto loaded = Config::from_json(path);

    EXPECT_DOUBLE_EQ(loaded.center_freq, original.center_freq);
    EXPECT_DOUBLE_EQ(loaded.sample_rate, original.sample_rate);
    EXPECT_DOUBLE_EQ(loaded.tx_gain, original.tx_gain);
    EXPECT_DOUBLE_EQ(loaded.rx_gain, original.rx_gain);
    EXPECT_EQ(loaded.tx_antenna, original.tx_antenna);
    EXPECT_EQ(loaded.rx_antenna, original.rx_antenna);
    EXPECT_EQ(loaded.samples_per_symbol, original.samples_per_symbol);
    EXPECT_DOUBLE_EQ(loaded.rrc_rolloff, original.rrc_rolloff);
    EXPECT_EQ(loaded.rrc_span_symbols, original.rrc_span_symbols);
    EXPECT_EQ(loaded.preamble, original.preamble);
    EXPECT_EQ(loaded.tcp_input_addr, original.tcp_input_addr);
    EXPECT_EQ(loaded.tcp_input_port, original.tcp_input_port);
    EXPECT_EQ(loaded.tcp_output_addr, original.tcp_output_addr);
    EXPECT_EQ(loaded.tcp_output_port, original.tcp_output_port);
    EXPECT_EQ(loaded.fec_enabled, original.fec_enabled);
    EXPECT_EQ(loaded.fec_code_rate, original.fec_code_rate);

    remove_file(path);
}

TEST(ConfigJson, RoundTripCustomValues) {
    auto path = temp_json_path("roundtrip_custom");
    Config original;
    original.center_freq       = 900e6;
    original.sample_rate       = 2e6;
    original.tx_gain           = 20.0;
    original.rx_gain           = 30.0;
    original.tx_antenna        = "TX/RX";
    original.rx_antenna        = "RX2";
    original.samples_per_symbol = 8;
    original.rrc_rolloff       = 0.5;
    original.rrc_span_symbols  = 10;
    original.preamble          = {1, 0, 1, 0, 1};
    original.tcp_input_addr    = "0.0.0.0";
    original.tcp_input_port    = 6000;
    original.tcp_output_addr   = "0.0.0.0";
    original.tcp_output_port   = 6001;
    original.fec_enabled       = false;
    original.fec_code_rate     = CodeRate::RATE_3_4;

    original.to_json(path);
    auto loaded = Config::from_json(path);

    EXPECT_DOUBLE_EQ(loaded.center_freq, original.center_freq);
    EXPECT_DOUBLE_EQ(loaded.sample_rate, original.sample_rate);
    EXPECT_DOUBLE_EQ(loaded.tx_gain, original.tx_gain);
    EXPECT_DOUBLE_EQ(loaded.rx_gain, original.rx_gain);
    EXPECT_EQ(loaded.tx_antenna, original.tx_antenna);
    EXPECT_EQ(loaded.rx_antenna, original.rx_antenna);
    EXPECT_EQ(loaded.samples_per_symbol, original.samples_per_symbol);
    EXPECT_DOUBLE_EQ(loaded.rrc_rolloff, original.rrc_rolloff);
    EXPECT_EQ(loaded.rrc_span_symbols, original.rrc_span_symbols);
    EXPECT_EQ(loaded.preamble, original.preamble);
    EXPECT_EQ(loaded.tcp_input_addr, original.tcp_input_addr);
    EXPECT_EQ(loaded.tcp_input_port, original.tcp_input_port);
    EXPECT_EQ(loaded.tcp_output_addr, original.tcp_output_addr);
    EXPECT_EQ(loaded.tcp_output_port, original.tcp_output_port);
    EXPECT_EQ(loaded.fec_enabled, original.fec_enabled);
    EXPECT_EQ(loaded.fec_code_rate, original.fec_code_rate);

    remove_file(path);
}

TEST(ConfigJson, RoundTripCodeRateHalf) {
    auto path = temp_json_path("roundtrip_rate12");
    auto cfg = Config::defaults();
    cfg.fec_code_rate = CodeRate::RATE_1_2;

    cfg.to_json(path);
    auto loaded = Config::from_json(path);
    EXPECT_EQ(loaded.fec_code_rate, CodeRate::RATE_1_2);

    remove_file(path);
}

TEST(ConfigJson, RoundTripCodeRateThreeQuarters) {
    auto path = temp_json_path("roundtrip_rate34");
    auto cfg = Config::defaults();
    cfg.fec_code_rate = CodeRate::RATE_3_4;

    cfg.to_json(path);
    auto loaded = Config::from_json(path);
    EXPECT_EQ(loaded.fec_code_rate, CodeRate::RATE_3_4);

    remove_file(path);
}

// ---------------------------------------------------------------------------
// Missing field detection
// ---------------------------------------------------------------------------

TEST(ConfigJson, MissingCenterFreq) {
    auto path = temp_json_path("missing_center_freq");
    // Write a JSON object missing center_freq
    auto cfg = Config::defaults();
    cfg.to_json(path);

    // Read, remove the field, rewrite
    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j.erase("center_freq");
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for missing center_freq";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("center_freq"), std::string::npos)
            << "Error should mention missing field: " << msg;
    }

    remove_file(path);
}

TEST(ConfigJson, MissingSampleRate) {
    auto path = temp_json_path("missing_sample_rate");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j.erase("sample_rate");
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for missing sample_rate";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("sample_rate"), std::string::npos)
            << "Error should mention missing field: " << msg;
    }

    remove_file(path);
}

TEST(ConfigJson, MissingFecCodeRate) {
    auto path = temp_json_path("missing_fec_code_rate");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j.erase("fec_code_rate");
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for missing fec_code_rate";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("fec_code_rate"), std::string::npos)
            << "Error should mention missing field: " << msg;
    }

    remove_file(path);
}

// ---------------------------------------------------------------------------
// Invalid field type detection
// ---------------------------------------------------------------------------

TEST(ConfigJson, InvalidTypeCenterFreqString) {
    auto path = temp_json_path("invalid_type_center_freq");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j["center_freq"] = "not_a_number";
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for string center_freq";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("center_freq"), std::string::npos)
            << "Error should mention field name: " << msg;
        EXPECT_NE(msg.find("number"), std::string::npos)
            << "Error should mention expected type: " << msg;
    }

    remove_file(path);
}

TEST(ConfigJson, InvalidTypeFecEnabledString) {
    auto path = temp_json_path("invalid_type_fec_enabled");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j["fec_enabled"] = "yes";
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for string fec_enabled";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("fec_enabled"), std::string::npos)
            << "Error should mention field name: " << msg;
        EXPECT_NE(msg.find("boolean"), std::string::npos)
            << "Error should mention expected type: " << msg;
    }

    remove_file(path);
}

TEST(ConfigJson, InvalidTypeSamplesPerSymbolFloat) {
    auto path = temp_json_path("invalid_type_sps");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j["samples_per_symbol"] = 4.5;
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for float samples_per_symbol";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("samples_per_symbol"), std::string::npos)
            << "Error should mention field name: " << msg;
    }

    remove_file(path);
}

TEST(ConfigJson, InvalidFecCodeRateValue) {
    auto path = temp_json_path("invalid_fec_code_rate_value");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j["fec_code_rate"] = "2/3";
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument for invalid fec_code_rate";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("fec_code_rate"), std::string::npos)
            << "Error should mention field name: " << msg;
    }

    remove_file(path);
}

// ---------------------------------------------------------------------------
// Invalid file path handling
// ---------------------------------------------------------------------------

TEST(ConfigJson, ToJsonInvalidPath) {
    auto cfg = Config::defaults();
    EXPECT_THROW(
        cfg.to_json("/nonexistent/directory/config.json"),
        std::invalid_argument);
}

TEST(ConfigJson, FromJsonNonexistentFile) {
    EXPECT_THROW(
        Config::from_json("/nonexistent/directory/config.json"),
        std::invalid_argument);
}

TEST(ConfigJson, FromJsonMalformedJson) {
    auto path = temp_json_path("malformed");
    write_raw(path, "{ this is not valid json }}}");

    EXPECT_THROW(Config::from_json(path), std::invalid_argument);

    remove_file(path);
}

// ---------------------------------------------------------------------------
// Validation is called after deserialization
// ---------------------------------------------------------------------------

TEST(ConfigJson, FromJsonCallsValidate) {
    auto path = temp_json_path("invalid_values");
    auto cfg = Config::defaults();
    cfg.to_json(path);

    // Modify the JSON to have an out-of-range center_freq
    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    j["center_freq"] = 50e6;  // below 70 MHz minimum
    write_raw(path, j.dump());

    try {
        Config::from_json(path);
        FAIL() << "Expected std::invalid_argument from validate()";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("center_freq"), std::string::npos)
            << "Error should come from validate(): " << msg;
    }

    remove_file(path);
}
