#include <gtest/gtest.h>
#include "qpsk_b200/types.h"

#include <stdexcept>
#include <string>

using namespace qpsk_b200;

// Helper: start from valid defaults, tweak one field, expect validation to throw
static Config valid_config() {
    return Config::defaults();
}

// ---------------------------------------------------------------------------
// Default config should validate successfully
// ---------------------------------------------------------------------------

TEST(ConfigValidate, DefaultsPassValidation) {
    auto cfg = valid_config();
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// center_freq validation: [70e6, 6e9]
// ---------------------------------------------------------------------------

TEST(ConfigValidate, CenterFreqTooLow) {
    auto cfg = valid_config();
    cfg.center_freq = 69e6;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, CenterFreqTooHigh) {
    auto cfg = valid_config();
    cfg.center_freq = 6.1e9;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, CenterFreqAtLowerBound) {
    auto cfg = valid_config();
    cfg.center_freq = 70e6;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ConfigValidate, CenterFreqAtUpperBound) {
    auto cfg = valid_config();
    cfg.center_freq = 6e9;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// sample_rate validation: (0, 56e6]
// ---------------------------------------------------------------------------

TEST(ConfigValidate, SampleRateZero) {
    auto cfg = valid_config();
    cfg.sample_rate = 0.0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, SampleRateNegative) {
    auto cfg = valid_config();
    cfg.sample_rate = -1.0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, SampleRateTooHigh) {
    auto cfg = valid_config();
    cfg.sample_rate = 57e6;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, SampleRateAtUpperBound) {
    auto cfg = valid_config();
    cfg.sample_rate = 56e6;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ConfigValidate, SampleRateSmallPositive) {
    auto cfg = valid_config();
    cfg.sample_rate = 1.0;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// tx_gain validation: [0, 89.75]
// ---------------------------------------------------------------------------

TEST(ConfigValidate, TxGainNegative) {
    auto cfg = valid_config();
    cfg.tx_gain = -0.1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, TxGainTooHigh) {
    auto cfg = valid_config();
    cfg.tx_gain = 90.0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, TxGainAtZero) {
    auto cfg = valid_config();
    cfg.tx_gain = 0.0;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ConfigValidate, TxGainAtMax) {
    auto cfg = valid_config();
    cfg.tx_gain = 89.75;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// rx_gain validation: [0, 76]
// ---------------------------------------------------------------------------

TEST(ConfigValidate, RxGainNegative) {
    auto cfg = valid_config();
    cfg.rx_gain = -0.1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RxGainTooHigh) {
    auto cfg = valid_config();
    cfg.rx_gain = 76.1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RxGainAtZero) {
    auto cfg = valid_config();
    cfg.rx_gain = 0.0;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ConfigValidate, RxGainAtMax) {
    auto cfg = valid_config();
    cfg.rx_gain = 76.0;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// samples_per_symbol validation: >= 2
// ---------------------------------------------------------------------------

TEST(ConfigValidate, SamplesPerSymbolTooLow) {
    auto cfg = valid_config();
    cfg.samples_per_symbol = 1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, SamplesPerSymbolZero) {
    auto cfg = valid_config();
    cfg.samples_per_symbol = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, SamplesPerSymbolAtMin) {
    auto cfg = valid_config();
    cfg.samples_per_symbol = 2;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// rrc_rolloff validation: (0, 1.0]
// ---------------------------------------------------------------------------

TEST(ConfigValidate, RrcRolloffZero) {
    auto cfg = valid_config();
    cfg.rrc_rolloff = 0.0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RrcRolloffNegative) {
    auto cfg = valid_config();
    cfg.rrc_rolloff = -0.1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RrcRolloffTooHigh) {
    auto cfg = valid_config();
    cfg.rrc_rolloff = 1.01;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RrcRolloffAtMax) {
    auto cfg = valid_config();
    cfg.rrc_rolloff = 1.0;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(ConfigValidate, RrcRolloffSmallPositive) {
    auto cfg = valid_config();
    cfg.rrc_rolloff = 0.01;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// rrc_span_symbols validation: >= 1
// ---------------------------------------------------------------------------

TEST(ConfigValidate, RrcSpanSymbolsZero) {
    auto cfg = valid_config();
    cfg.rrc_span_symbols = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RrcSpanSymbolsNegative) {
    auto cfg = valid_config();
    cfg.rrc_span_symbols = -1;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, RrcSpanSymbolsAtMin) {
    auto cfg = valid_config();
    cfg.rrc_span_symbols = 1;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// tcp_input_port validation: > 0
// ---------------------------------------------------------------------------

TEST(ConfigValidate, TcpInputPortZero) {
    auto cfg = valid_config();
    cfg.tcp_input_port = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, TcpInputPortValid) {
    auto cfg = valid_config();
    cfg.tcp_input_port = 1;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// tcp_output_port validation: > 0
// ---------------------------------------------------------------------------

TEST(ConfigValidate, TcpOutputPortZero) {
    auto cfg = valid_config();
    cfg.tcp_output_port = 0;
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, TcpOutputPortValid) {
    auto cfg = valid_config();
    cfg.tcp_output_port = 1;
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// preamble validation: must not be empty
// ---------------------------------------------------------------------------

TEST(ConfigValidate, PreambleEmpty) {
    auto cfg = valid_config();
    cfg.preamble.clear();
    EXPECT_THROW(cfg.validate(), std::invalid_argument);
}

TEST(ConfigValidate, PreambleSingleElement) {
    auto cfg = valid_config();
    cfg.preamble = {1};
    EXPECT_NO_THROW(cfg.validate());
}

// ---------------------------------------------------------------------------
// Error message content checks
// ---------------------------------------------------------------------------

TEST(ConfigValidate, CenterFreqErrorMessageContainsFieldName) {
    auto cfg = valid_config();
    cfg.center_freq = 50e6;
    try {
        cfg.validate();
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("center_freq"), std::string::npos)
            << "Error message should contain field name: " << msg;
    }
}

TEST(ConfigValidate, SampleRateErrorMessageContainsFieldName) {
    auto cfg = valid_config();
    cfg.sample_rate = -5.0;
    try {
        cfg.validate();
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("sample_rate"), std::string::npos)
            << "Error message should contain field name: " << msg;
    }
}
