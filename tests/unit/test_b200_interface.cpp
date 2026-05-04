#include <gtest/gtest.h>

#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

#include "qpsk_b200/b200_interface.h"
#include "qpsk_b200/types.h"

namespace qpsk_b200 {
namespace {

// =========================================================================
// Tests that run WITHOUT UHD (stub behavior)
// =========================================================================
#ifndef QPSK_HAS_UHD

class B200InterfaceStubTest : public ::testing::Test {
protected:
    Config cfg = Config::defaults();
};

// The stub constructor should NOT throw — it just logs a warning.
TEST_F(B200InterfaceStubTest, ConstructorDoesNotThrow) {
    EXPECT_NO_THROW(B200Interface iface(cfg));
}

// configure_tx should throw with the expected UHD-not-available message.
TEST_F(B200InterfaceStubTest, ConfigureTxThrowsWithoutUhd) {
    B200Interface iface(cfg);
    try {
        iface.configure_tx(cfg);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("UHD"), std::string::npos)
            << "Error message should mention UHD: " << msg;
        EXPECT_NE(msg.find("QPSK_ENABLE_UHD"), std::string::npos)
            << "Error message should mention the CMake option: " << msg;
    }
}

// configure_rx should throw with the expected UHD-not-available message.
TEST_F(B200InterfaceStubTest, ConfigureRxThrowsWithoutUhd) {
    B200Interface iface(cfg);
    try {
        iface.configure_rx(cfg);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("UHD"), std::string::npos)
            << "Error message should mention UHD: " << msg;
    }
}

// send should throw with the expected UHD-not-available message.
TEST_F(B200InterfaceStubTest, SendThrowsWithoutUhd) {
    B200Interface iface(cfg);
    std::vector<std::complex<float>> samples = {{1.0f, 0.0f}, {0.0f, 1.0f}};
    try {
        iface.send(samples);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("UHD"), std::string::npos)
            << "Error message should mention UHD: " << msg;
    }
}

// recv should throw with the expected UHD-not-available message.
TEST_F(B200InterfaceStubTest, RecvThrowsWithoutUhd) {
    B200Interface iface(cfg);
    try {
        iface.recv(100);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("UHD"), std::string::npos)
            << "Error message should mention UHD: " << msg;
    }
}

// start_rx_stream should throw with the expected UHD-not-available message.
TEST_F(B200InterfaceStubTest, StartRxStreamThrowsWithoutUhd) {
    B200Interface iface(cfg);
    try {
        iface.start_rx_stream();
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("UHD"), std::string::npos)
            << "Error message should mention UHD: " << msg;
    }
}

// stop_rx_stream should throw with the expected UHD-not-available message.
TEST_F(B200InterfaceStubTest, StopRxStreamThrowsWithoutUhd) {
    B200Interface iface(cfg);
    try {
        iface.stop_rx_stream();
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("UHD"), std::string::npos)
            << "Error message should mention UHD: " << msg;
    }
}

// All stub methods should throw the same consistent error message.
TEST_F(B200InterfaceStubTest, AllMethodsThrowConsistentMessage) {
    B200Interface iface(cfg);
    const std::string expected_substr = "UHD support not compiled";

    auto check_throws = [&](auto fn, const char* method_name) {
        try {
            fn();
            FAIL() << method_name << " should have thrown";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find(expected_substr),
                       std::string::npos)
                << method_name << " error message mismatch: " << e.what();
        }
    };

    check_throws([&]() { iface.configure_tx(cfg); }, "configure_tx");
    check_throws([&]() { iface.configure_rx(cfg); }, "configure_rx");
    check_throws([&]() { iface.send({}); }, "send");
    check_throws([&]() { iface.recv(1); }, "recv");
    check_throws([&]() { iface.start_rx_stream(); }, "start_rx_stream");
    check_throws([&]() { iface.stop_rx_stream(); }, "stop_rx_stream");
}

// Verify the stub can be move-constructed.
TEST_F(B200InterfaceStubTest, MoveConstructible) {
    B200Interface iface1(cfg);
    B200Interface iface2(std::move(iface1));
    // The moved-to object should still throw on method calls
    EXPECT_THROW(iface2.configure_tx(cfg), std::runtime_error);
}

// Verify the stub can be move-assigned.
TEST_F(B200InterfaceStubTest, MoveAssignable) {
    B200Interface iface1(cfg);
    B200Interface iface2(cfg);
    iface2 = std::move(iface1);
    EXPECT_THROW(iface2.send({}), std::runtime_error);
}

#endif // !QPSK_HAS_UHD

// =========================================================================
// Tests that run WITH UHD (mock-based)
// These are conditionally compiled — they only build when UHD is available.
// =========================================================================
#ifdef QPSK_HAS_UHD

class B200InterfaceUhdTest : public ::testing::Test {
protected:
    Config cfg = Config::defaults();
};

// When UHD is available but no device is connected, the constructor
// should throw a descriptive error.
TEST_F(B200InterfaceUhdTest, ThrowsWhenNoDeviceFound) {
    // This test only makes sense when UHD is compiled in but no
    // physical B200 is attached. In CI environments without hardware,
    // uhd::device::find() returns an empty list.
    try {
        B200Interface iface(cfg);
        // If a device IS found (e.g., running on a machine with B200),
        // the constructor succeeds — that's fine, skip the assertion.
        GTEST_SKIP() << "B200 device found — skipping no-device test";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("No B200 device found"), std::string::npos)
            << "Error message should mention device not found: " << msg;
    }
}

#endif // QPSK_HAS_UHD

} // anonymous namespace
} // namespace qpsk_b200
