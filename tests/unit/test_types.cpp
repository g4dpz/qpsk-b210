#include <gtest/gtest.h>
#include "qpsk_b200/types.h"

#include <vector>
#include <cstdint>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Config::defaults() — verify factory defaults match Requirements 8.6–8.9
// ---------------------------------------------------------------------------

TEST(ConfigDefaults, CenterFrequency) {
    auto cfg = Config::defaults();
    EXPECT_DOUBLE_EQ(cfg.center_freq, 2.4e9);
}

TEST(ConfigDefaults, SampleRate) {
    auto cfg = Config::defaults();
    EXPECT_DOUBLE_EQ(cfg.sample_rate, 1e6);
}

TEST(ConfigDefaults, TxGain) {
    auto cfg = Config::defaults();
    EXPECT_DOUBLE_EQ(cfg.tx_gain, 40.0);
}

TEST(ConfigDefaults, RxGain) {
    auto cfg = Config::defaults();
    EXPECT_DOUBLE_EQ(cfg.rx_gain, 40.0);
}

TEST(ConfigDefaults, SamplesPerSymbol) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.samples_per_symbol, 4);
}

TEST(ConfigDefaults, RrcRolloff) {
    auto cfg = Config::defaults();
    EXPECT_DOUBLE_EQ(cfg.rrc_rolloff, 0.35);
}

TEST(ConfigDefaults, FecEnabled) {
    auto cfg = Config::defaults();
    EXPECT_TRUE(cfg.fec_enabled);
}

TEST(ConfigDefaults, FecCodeRate) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.fec_code_rate, CodeRate::RATE_1_2);
}

TEST(ConfigDefaults, TcpInputAddr) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.tcp_input_addr, "127.0.0.1");
}

TEST(ConfigDefaults, TcpInputPort) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.tcp_input_port, 5000);
}

TEST(ConfigDefaults, TcpOutputAddr) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.tcp_output_addr, "127.0.0.1");
}

TEST(ConfigDefaults, TcpOutputPort) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.tcp_output_port, 5001);
}

TEST(ConfigDefaults, TxAntenna) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.tx_antenna, "TX/RX");
}

TEST(ConfigDefaults, RxAntenna) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.rx_antenna, "RX2");
}

TEST(ConfigDefaults, Preamble) {
    auto cfg = Config::defaults();
    std::vector<uint8_t> expected = {1,1,1,1,1,0,0,1,1,0,1,0,1};
    EXPECT_EQ(cfg.preamble, expected);
}

TEST(ConfigDefaults, RrcSpanSymbols) {
    auto cfg = Config::defaults();
    EXPECT_EQ(cfg.rrc_span_symbols, 6);
}

// ---------------------------------------------------------------------------
// CodeRate enum values
// ---------------------------------------------------------------------------

TEST(CodeRate, EnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(CodeRate::RATE_1_2), 0);
    EXPECT_EQ(static_cast<uint8_t>(CodeRate::RATE_3_4), 1);
}

// ---------------------------------------------------------------------------
// Diagnostic struct defaults (all counters start at zero)
// ---------------------------------------------------------------------------

TEST(TxDiagnostics, DefaultsAreZero) {
    TxDiagnostics diag;
    EXPECT_EQ(diag.frames_transmitted, 0u);
    EXPECT_EQ(diag.symbols_transmitted, 0u);
}

TEST(RxDiagnostics, DefaultsAreZero) {
    RxDiagnostics diag;
    EXPECT_EQ(diag.frames_received, 0u);
    EXPECT_EQ(diag.crc_errors, 0u);
    EXPECT_DOUBLE_EQ(diag.estimated_ber, 0.0);
    EXPECT_DOUBLE_EQ(diag.last_freq_offset, 0.0);
    EXPECT_DOUBLE_EQ(diag.last_phase_offset, 0.0);
    EXPECT_DOUBLE_EQ(diag.last_timing_offset, 0.0);
    EXPECT_EQ(diag.fec_errors_corrected, 0u);
}

TEST(TcpDiagnostics, DefaultsAreZero) {
    TcpDiagnostics diag;
    EXPECT_EQ(diag.clients_connected, 0u);
    EXPECT_EQ(diag.clients_disconnected, 0u);
    EXPECT_EQ(diag.bytes_received, 0u);
    EXPECT_EQ(diag.bytes_sent, 0u);
}

// ---------------------------------------------------------------------------
// FrameHeader / Frame default construction
// ---------------------------------------------------------------------------

TEST(FrameHeader, DefaultConstruction) {
    FrameHeader hdr;
    EXPECT_EQ(hdr.payload_length, 0);
    EXPECT_EQ(hdr.padding_bits, 0);
    EXPECT_EQ(hdr.sequence_number, 0);
    EXPECT_FALSE(hdr.fec_enabled);
    EXPECT_EQ(hdr.fec_code_rate, CodeRate::RATE_1_2);
}

TEST(Frame, DefaultConstruction) {
    Frame frame;
    EXPECT_EQ(frame.header.payload_length, 0);
    EXPECT_TRUE(frame.payload.empty());
    EXPECT_EQ(frame.crc, 0u);
    EXPECT_FALSE(frame.crc_valid);
}
