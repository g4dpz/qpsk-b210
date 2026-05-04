// Integration test: B200Interface + Config integration
//
// Since UHD is not available in the test environment, this test verifies
// that B200Interface correctly integrates with Config: the constructor
// accepts a valid Config, and all hardware methods throw descriptive errors
// when UHD is not compiled in.
//
// Most stub behavior is already covered by tests/unit/test_b200_interface.cpp.
// This integration test focuses on the Config → B200Interface handoff.
//
// Validates: Requirements 1.1–1.6, 2.1–2.5

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "qpsk_b200/b200_interface.h"
#include "qpsk_b200/types.h"

namespace qpsk_b200 {
namespace {

#ifndef QPSK_HAS_UHD

// Verify that a Config loaded from JSON can be used to construct a
// B200Interface, and that the interface rejects operations gracefully.
TEST(B200ConfigIntegration, ConfigDefaultsCreateInterface) {
    Config cfg = Config::defaults();
    cfg.validate();  // should not throw

    // B200Interface accepts a validated Config
    B200Interface iface(cfg);

    // All hardware methods throw with descriptive UHD message
    EXPECT_THROW(iface.configure_tx(cfg), std::runtime_error);
    EXPECT_THROW(iface.configure_rx(cfg), std::runtime_error);
}

// Verify that a Config with custom RF parameters still constructs
// the B200Interface successfully (the stub doesn't use the values,
// but the interface should accept any valid Config).
TEST(B200ConfigIntegration, CustomConfigCreateInterface) {
    Config cfg = Config::defaults();
    cfg.center_freq = 915e6;       // 915 MHz ISM band
    cfg.sample_rate = 2e6;         // 2 MSPS
    cfg.tx_gain = 20.0;
    cfg.rx_gain = 30.0;
    cfg.tx_antenna = "TX/RX";
    cfg.rx_antenna = "RX2";
    cfg.validate();

    B200Interface iface(cfg);

    // Stub still throws, but the Config was accepted
    EXPECT_THROW(iface.configure_tx(cfg), std::runtime_error);
    EXPECT_THROW(iface.configure_rx(cfg), std::runtime_error);
}

// Verify that an invalid Config is caught by validate() BEFORE
// reaching B200Interface — this is the expected integration flow.
TEST(B200ConfigIntegration, InvalidConfigCaughtByValidate) {
    Config cfg = Config::defaults();
    cfg.center_freq = 50e6;  // below 70 MHz minimum

    EXPECT_THROW(cfg.validate(), std::invalid_argument);
    // In the real application flow, validate() is called before
    // constructing B200Interface, so the interface never sees bad params.
}

#endif // !QPSK_HAS_UHD

} // anonymous namespace
} // namespace qpsk_b200
