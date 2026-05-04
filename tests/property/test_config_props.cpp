// Feature: qpsk-b200-codec, Property 1: Configuration parameter validation
//
// For any configuration parameter value that falls outside the documented valid
// range, Config::validate() SHALL reject the configuration and throw
// std::invalid_argument. Conversely, for any parameter value within the valid
// range, validation SHALL succeed.
//
// **Validates: Requirements 1.6, 2.5, 8.5**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/types.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Generators for valid parameter values
// ---------------------------------------------------------------------------

static rc::Gen<double> genValidCenterFreq() {
    return rc::gen::map(rc::gen::inRange(0, 1000001), [](int v) {
        // Map [0, 1000000] → [70e6, 6e9]
        return 70e6 + (static_cast<double>(v) / 1000000.0) * (6e9 - 70e6);
    });
}

static rc::Gen<double> genValidSampleRate() {
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        // Map [1, 1000000] → (0, 56e6]  (never exactly 0)
        return (static_cast<double>(v) / 1000000.0) * 56e6;
    });
}

static rc::Gen<double> genValidTxGain() {
    return rc::gen::map(rc::gen::inRange(0, 100001), [](int v) {
        return (static_cast<double>(v) / 100000.0) * 89.75;
    });
}

static rc::Gen<double> genValidRxGain() {
    return rc::gen::map(rc::gen::inRange(0, 100001), [](int v) {
        return (static_cast<double>(v) / 100000.0) * 76.0;
    });
}

static rc::Gen<int> genValidSamplesPerSymbol() {
    return rc::gen::inRange(2, 33); // [2, 32]
}

static rc::Gen<double> genValidRrcRolloff() {
    return rc::gen::map(rc::gen::inRange(1, 100001), [](int v) {
        // Map [1, 100000] → (0, 1.0]  (never exactly 0)
        return static_cast<double>(v) / 100000.0;
    });
}

static rc::Gen<int> genValidRrcSpanSymbols() {
    return rc::gen::inRange(1, 21); // [1, 20]
}

static rc::Gen<uint16_t> genValidPort() {
    return rc::gen::inRange<uint16_t>(1, 65535);
}

static rc::Gen<std::vector<uint8_t>> genValidPreamble() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::vector<uint8_t>>(rc::gen::inRange<uint8_t>(0, 2)));
}

// Generator for a fully valid Config
static rc::Gen<Config> genValidConfig() {
    return rc::gen::build<Config>(
        rc::gen::set(&Config::center_freq, genValidCenterFreq()),
        rc::gen::set(&Config::sample_rate, genValidSampleRate()),
        rc::gen::set(&Config::tx_gain, genValidTxGain()),
        rc::gen::set(&Config::rx_gain, genValidRxGain()),
        rc::gen::set(&Config::samples_per_symbol, genValidSamplesPerSymbol()),
        rc::gen::set(&Config::rrc_rolloff, genValidRrcRolloff()),
        rc::gen::set(&Config::rrc_span_symbols, genValidRrcSpanSymbols()),
        rc::gen::set(&Config::tcp_input_port, genValidPort()),
        rc::gen::set(&Config::tcp_output_port, genValidPort()),
        rc::gen::set(&Config::preamble, genValidPreamble())
    );
}

// ---------------------------------------------------------------------------
// Generators for invalid parameter values (outside valid ranges)
// ---------------------------------------------------------------------------

static rc::Gen<double> genInvalidCenterFreqLow() {
    // Below 70 MHz
    return rc::gen::map(rc::gen::inRange(0, 1000000), [](int v) {
        return static_cast<double>(v) / 1000000.0 * 70e6 - 1.0;
    });
}

static rc::Gen<double> genInvalidCenterFreqHigh() {
    // Above 6 GHz
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return 6e9 + static_cast<double>(v);
    });
}

static rc::Gen<double> genInvalidSampleRateLow() {
    // <= 0
    return rc::gen::map(rc::gen::inRange(0, 1000001), [](int v) {
        return -static_cast<double>(v);
    });
}

static rc::Gen<double> genInvalidSampleRateHigh() {
    // > 56e6
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return 56e6 + static_cast<double>(v);
    });
}

static rc::Gen<double> genInvalidTxGainLow() {
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return -static_cast<double>(v) / 100.0;
    });
}

static rc::Gen<double> genInvalidTxGainHigh() {
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return 89.75 + static_cast<double>(v) / 100.0;
    });
}

static rc::Gen<double> genInvalidRxGainLow() {
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return -static_cast<double>(v) / 100.0;
    });
}

static rc::Gen<double> genInvalidRxGainHigh() {
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return 76.0 + static_cast<double>(v) / 100.0;
    });
}

static rc::Gen<int> genInvalidSamplesPerSymbol() {
    // < 2 (including negative)
    return rc::gen::inRange(-100, 2);
}

static rc::Gen<double> genInvalidRrcRolloffLow() {
    // <= 0
    return rc::gen::map(rc::gen::inRange(0, 1000001), [](int v) {
        return -static_cast<double>(v) / 100.0;
    });
}

static rc::Gen<double> genInvalidRrcRolloffHigh() {
    // > 1.0
    return rc::gen::map(rc::gen::inRange(1, 1000001), [](int v) {
        return 1.0 + static_cast<double>(v) / 100000.0;
    });
}

static rc::Gen<int> genInvalidRrcSpanSymbols() {
    // < 1
    return rc::gen::inRange(-100, 1);
}

// ---------------------------------------------------------------------------
// Property: All parameters within valid ranges → validate() succeeds
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ConfigPropertyTest, AllValidParamsAccepted, ()) {
    auto cfg = *genValidConfig();
    RC_ASSERT_FALSE(cfg.preamble.empty());
    cfg.validate(); // Should not throw
}

// ---------------------------------------------------------------------------
// Properties: One parameter outside valid range → validate() throws
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ConfigPropertyTest, InvalidCenterFreqLowRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.center_freq = *genInvalidCenterFreqLow();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidCenterFreqHighRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.center_freq = *genInvalidCenterFreqHigh();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidSampleRateLowRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.sample_rate = *genInvalidSampleRateLow();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidSampleRateHighRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.sample_rate = *genInvalidSampleRateHigh();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidTxGainLowRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.tx_gain = *genInvalidTxGainLow();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidTxGainHighRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.tx_gain = *genInvalidTxGainHigh();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidRxGainLowRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.rx_gain = *genInvalidRxGainLow();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidRxGainHighRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.rx_gain = *genInvalidRxGainHigh();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidSamplesPerSymbolRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.samples_per_symbol = *genInvalidSamplesPerSymbol();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidRrcRolloffLowRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.rrc_rolloff = *genInvalidRrcRolloffLow();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidRrcRolloffHighRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.rrc_rolloff = *genInvalidRrcRolloffHigh();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidRrcSpanSymbolsRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.rrc_span_symbols = *genInvalidRrcSpanSymbols();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidTcpInputPortRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.tcp_input_port = 0;
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, InvalidTcpOutputPortRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.tcp_output_port = 0;
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

RC_GTEST_PROP(ConfigPropertyTest, EmptyPreambleRejected, ()) {
    auto cfg = *genValidConfig();
    cfg.preamble.clear();
    RC_ASSERT_THROWS_AS(cfg.validate(), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Feature: qpsk-b200-codec, Property 13: Configuration serialization round trip
//
// For any valid Config object, serializing to JSON via Config::to_json() and
// then deserializing via Config::from_json() SHALL produce a Config object that
// is field-by-field equivalent to the original.
//
// **Validates: Requirements 10.1, 10.2, 10.3**
// ---------------------------------------------------------------------------

#include <cstdio>

// Generator for a random CodeRate value
static rc::Gen<CodeRate> genValidCodeRate() {
    return rc::gen::element(CodeRate::RATE_1_2, CodeRate::RATE_3_4);
}

// Generator for a non-empty string (for antenna / address fields)
static rc::Gen<std::string> genNonEmptyAlphaString() {
    return rc::gen::nonEmpty(
        rc::gen::container<std::string>(rc::gen::inRange<char>('a', 'z' + 1)));
}

RC_GTEST_PROP(ConfigPropertyTest, SerializationRoundTrip, ()) {
    auto original = *genValidConfig();

    // Also randomize fields not covered by genValidConfig()
    original.fec_enabled   = *rc::gen::arbitrary<bool>();
    original.fec_code_rate = *genValidCodeRate();
    original.tx_antenna    = *genNonEmptyAlphaString();
    original.rx_antenna    = *genNonEmptyAlphaString();
    original.tcp_input_addr  = *genNonEmptyAlphaString();
    original.tcp_output_addr = *genNonEmptyAlphaString();

    // Write to temp file
    std::string path = "/tmp/test_config_roundtrip_" +
                       std::to_string(rc::gen::arbitrary<uint32_t>().operator*()) +
                       ".json";
    original.to_json(path);
    auto loaded = Config::from_json(path);

    // Verify field-by-field equivalence (exact equality for doubles —
    // nlohmann/json preserves full double precision on round trip)
    RC_ASSERT(loaded.center_freq       == original.center_freq);
    RC_ASSERT(loaded.sample_rate       == original.sample_rate);
    RC_ASSERT(loaded.tx_gain           == original.tx_gain);
    RC_ASSERT(loaded.rx_gain           == original.rx_gain);
    RC_ASSERT(loaded.tx_antenna        == original.tx_antenna);
    RC_ASSERT(loaded.rx_antenna        == original.rx_antenna);
    RC_ASSERT(loaded.samples_per_symbol == original.samples_per_symbol);
    RC_ASSERT(loaded.rrc_rolloff       == original.rrc_rolloff);
    RC_ASSERT(loaded.rrc_span_symbols  == original.rrc_span_symbols);
    RC_ASSERT(loaded.preamble          == original.preamble);
    RC_ASSERT(loaded.tcp_input_addr    == original.tcp_input_addr);
    RC_ASSERT(loaded.tcp_input_port    == original.tcp_input_port);
    RC_ASSERT(loaded.tcp_output_addr   == original.tcp_output_addr);
    RC_ASSERT(loaded.tcp_output_port   == original.tcp_output_port);
    RC_ASSERT(loaded.fec_enabled       == original.fec_enabled);
    RC_ASSERT(loaded.fec_code_rate     == original.fec_code_rate);

    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Feature: qpsk-b200-codec, Property 14: Invalid JSON error reporting
//
// For any JSON object that is missing one or more required configuration fields
// or contains a field with an out-of-range value, Config::from_json() SHALL
// return a descriptive error message identifying the invalid or missing field(s).
//
// **Validates: Requirements 10.4**
// ---------------------------------------------------------------------------

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

// All required fields in the Config JSON
static const std::vector<std::string> REQUIRED_FIELDS = {
    "center_freq", "sample_rate", "tx_gain", "rx_gain",
    "tx_antenna", "rx_antenna", "samples_per_symbol", "rrc_rolloff",
    "rrc_span_symbols", "preamble", "tcp_input_addr", "tcp_input_port",
    "tcp_output_addr", "tcp_output_port", "fec_enabled", "fec_code_rate"
};

// Numeric fields that can be replaced with a string to trigger type errors
static const std::vector<std::string> NUMERIC_FIELDS = {
    "center_freq", "sample_rate", "tx_gain", "rx_gain",
    "samples_per_symbol", "rrc_rolloff", "rrc_span_symbols",
    "tcp_input_port", "tcp_output_port"
};

// Helper: write a nlohmann::json object to a temp file and return the path
static std::string writeJsonToTempFile(const nlohmann::json& j,
                                       const std::string& suffix) {
    std::string path = "/tmp/test_config_prop14_" + suffix + "_" +
                       std::to_string(
                           rc::gen::arbitrary<uint32_t>().operator*()) +
                       ".json";
    std::ofstream ofs(path);
    ofs << j.dump(4);
    ofs.close();
    return path;
}

// Generator: pick a random index into a vector of strings
static rc::Gen<size_t> genFieldIndex(size_t count) {
    return rc::gen::inRange<size_t>(0, count);
}

RC_GTEST_PROP(ConfigPropertyTest,
              MissingRequiredFieldThrowsWithFieldName, ()) {
    // 1. Generate a valid Config and serialize to JSON
    auto cfg = *genValidConfig();
    cfg.fec_enabled   = *rc::gen::arbitrary<bool>();
    cfg.fec_code_rate = *genValidCodeRate();
    cfg.tx_antenna    = *genNonEmptyAlphaString();
    cfg.rx_antenna    = *genNonEmptyAlphaString();
    cfg.tcp_input_addr  = *genNonEmptyAlphaString();
    cfg.tcp_output_addr = *genNonEmptyAlphaString();

    // Serialize to JSON via the Config method, then read back as nlohmann::json
    std::string origPath = "/tmp/test_config_prop14_orig_" +
                           std::to_string(
                               rc::gen::arbitrary<uint32_t>().operator*()) +
                           ".json";
    cfg.to_json(origPath);

    std::ifstream ifs(origPath);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    std::remove(origPath.c_str());

    // 2. Randomly remove one required field
    size_t idx = *genFieldIndex(REQUIRED_FIELDS.size());
    std::string removedField = REQUIRED_FIELDS[idx];
    j.erase(removedField);

    // 3. Write modified JSON to temp file
    std::string modPath = writeJsonToTempFile(j, "missing");

    // 4. from_json() should throw std::invalid_argument
    //    with the removed field name in the error message
    try {
        Config::from_json(modPath);
        std::remove(modPath.c_str());
        RC_FAIL("Expected std::invalid_argument but no exception was thrown");
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        RC_ASSERT(msg.find(removedField) != std::string::npos);
        std::remove(modPath.c_str());
    } catch (...) {
        std::remove(modPath.c_str());
        RC_FAIL("Expected std::invalid_argument but got a different exception");
    }
}

RC_GTEST_PROP(ConfigPropertyTest,
              NumericFieldReplacedWithStringThrowsWithFieldName, ()) {
    // 1. Generate a valid Config and serialize to JSON
    auto cfg = *genValidConfig();
    cfg.fec_enabled   = *rc::gen::arbitrary<bool>();
    cfg.fec_code_rate = *genValidCodeRate();
    cfg.tx_antenna    = *genNonEmptyAlphaString();
    cfg.rx_antenna    = *genNonEmptyAlphaString();
    cfg.tcp_input_addr  = *genNonEmptyAlphaString();
    cfg.tcp_output_addr = *genNonEmptyAlphaString();

    // Serialize to JSON via the Config method, then read back as nlohmann::json
    std::string origPath = "/tmp/test_config_prop14_orig2_" +
                           std::to_string(
                               rc::gen::arbitrary<uint32_t>().operator*()) +
                           ".json";
    cfg.to_json(origPath);

    std::ifstream ifs(origPath);
    nlohmann::json j;
    ifs >> j;
    ifs.close();
    std::remove(origPath.c_str());

    // 2. Randomly replace one numeric field with a string value
    size_t idx = *genFieldIndex(NUMERIC_FIELDS.size());
    std::string corruptedField = NUMERIC_FIELDS[idx];
    j[corruptedField] = "not_a_number";

    // 3. Write modified JSON to temp file
    std::string modPath = writeJsonToTempFile(j, "badtype");

    // 4. from_json() should throw std::invalid_argument
    //    with the corrupted field name in the error message
    try {
        Config::from_json(modPath);
        std::remove(modPath.c_str());
        RC_FAIL("Expected std::invalid_argument but no exception was thrown");
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        RC_ASSERT(msg.find(corruptedField) != std::string::npos);
        std::remove(modPath.c_str());
    } catch (...) {
        std::remove(modPath.c_str());
        RC_FAIL("Expected std::invalid_argument but got a different exception");
    }
}
