#include "qpsk_b200/frame.h"
#include "qpsk_b200/frame_sync.h"

#include <gtest/gtest.h>
#include <complex>
#include <cstdint>
#include <optional>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Default preamble: 13-bit Barker × 2 = 26 bits
// ---------------------------------------------------------------------------
static const std::vector<uint8_t> BARKER_13 = {1,1,1,1,1,0,0,1,1,0,1,0,1};

static std::vector<uint8_t> double_barker() {
    std::vector<uint8_t> p;
    p.insert(p.end(), BARKER_13.begin(), BARKER_13.end());
    p.insert(p.end(), BARKER_13.begin(), BARKER_13.end());
    return p;
}

// ===========================================================================
// FrameBuilder tests
// ===========================================================================

TEST(FrameBuilderTest, BuildFrameStartsWithPreamble) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0xAB, 0xCD};
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    // First 26 bits should match the preamble
    ASSERT_GE(bits.size(), preamble.size());
    for (size_t i = 0; i < preamble.size(); ++i) {
        EXPECT_EQ(bits[i], preamble[i]) << "Preamble mismatch at bit " << i;
    }
}

TEST(FrameBuilderTest, BuildFrameCorrectTotalLength) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    // Expected: preamble(26) + header(35) + payload(3*8=24) + CRC(32) = 117
    size_t expected = 26 + 35 + 24 + 32;
    EXPECT_EQ(bits.size(), expected);
}

TEST(FrameBuilderTest, BuildFrameEmptyPayload) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload;
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    // Expected: preamble(26) + header(35) + payload(0) + CRC(32) = 93
    EXPECT_EQ(bits.size(), 93u);
}

TEST(FrameBuilderTest, SequenceNumberIncrements) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    EXPECT_EQ(builder.current_sequence_number(), 0u);

    std::vector<uint8_t> payload = {0x42};
    builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);
    EXPECT_EQ(builder.current_sequence_number(), 1u);

    builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);
    EXPECT_EQ(builder.current_sequence_number(), 2u);

    builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);
    EXPECT_EQ(builder.current_sequence_number(), 3u);
}

TEST(FrameBuilderTest, SequenceNumberWrapsAt4096) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0x00};
    // Build 4096 frames to wrap the sequence number
    for (int i = 0; i < 4096; ++i) {
        builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);
    }
    EXPECT_EQ(builder.current_sequence_number(), 0u);
}

TEST(FrameBuilderTest, CRC32KnownValue) {
    // CRC-32 of empty data should be 0x00000000 ... actually let's use a known test vector.
    // CRC-32 of "123456789" (ASCII) = 0xCBF43926
    std::vector<uint8_t> data = {'1','2','3','4','5','6','7','8','9'};
    uint32_t crc = FrameBuilder::compute_crc32(data);
    EXPECT_EQ(crc, 0xCBF43926u);
}

TEST(FrameBuilderTest, CRC32EmptyData) {
    std::vector<uint8_t> data;
    uint32_t crc = FrameBuilder::compute_crc32(data);
    // CRC-32 of empty data = 0x00000000
    EXPECT_EQ(crc, 0x00000000u);
}

TEST(FrameBuilderTest, HeaderContainsCorrectFrameLength) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload(100, 0xAA);  // 100 bytes
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    // Frame length is at bits[26..41] (16 bits, MSB first)
    uint16_t frame_length = 0;
    for (int i = 0; i < 16; ++i) {
        frame_length = (frame_length << 1) | bits[26 + i];
    }
    EXPECT_EQ(frame_length, 100u);
}

TEST(FrameBuilderTest, HeaderContainsFECFields) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0x01};
    auto bits = builder.build_frame(payload, 3, true, CodeRate::RATE_3_4);

    // Padding bits at bits[42..45] (4 bits)
    uint8_t pad = 0;
    for (int i = 0; i < 4; ++i) {
        pad = (pad << 1) | bits[42 + i];
    }
    EXPECT_EQ(pad, 3u);

    // FEC enabled at bit[46]
    EXPECT_EQ(bits[46], 1u);

    // FEC code rate at bits[47..48] (2 bits)
    uint8_t rate = (bits[47] << 1) | bits[48];
    EXPECT_EQ(rate, static_cast<uint8_t>(CodeRate::RATE_3_4));
}

// ===========================================================================
// FrameParser tests
// ===========================================================================

TEST(FrameParserTest, ParseExtractsCorrectFields) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto bits = builder.build_frame(payload, 2, true, CodeRate::RATE_3_4);

    // Strip preamble for parser
    std::vector<uint8_t> after_preamble(bits.begin() + preamble.size(), bits.end());

    auto result = FrameParser::parse(after_preamble);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->header.payload_length, 4u);
    EXPECT_EQ(result->header.padding_bits, 2u);
    EXPECT_TRUE(result->header.fec_enabled);
    EXPECT_EQ(result->header.fec_code_rate, CodeRate::RATE_3_4);
    EXPECT_EQ(result->header.sequence_number, 0u);
    EXPECT_EQ(result->payload, payload);
    EXPECT_TRUE(result->crc_valid);
}

TEST(FrameParserTest, ParseReturnsNulloptForTooShortInput) {
    // Less than header(35) + CRC(32) = 67 bits
    std::vector<uint8_t> short_bits(60, 0);
    auto result = FrameParser::parse(short_bits);
    EXPECT_FALSE(result.has_value());
}

TEST(FrameParserTest, ParseReturnsNulloptOnCRCMismatch) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0x42};
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    // Strip preamble
    std::vector<uint8_t> after_preamble(bits.begin() + preamble.size(), bits.end());

    // Corrupt a payload bit
    if (after_preamble.size() > 36) {
        after_preamble[36] ^= 1;  // flip a payload bit
    }

    auto result = FrameParser::parse(after_preamble);
    EXPECT_FALSE(result.has_value());
}

TEST(FrameParserTest, VerifyCRCOnValidFrame) {
    Frame frame;
    frame.header.payload_length = 2;
    frame.header.padding_bits = 0;
    frame.header.fec_enabled = false;
    frame.header.fec_code_rate = CodeRate::RATE_1_2;
    frame.header.sequence_number = 42;
    frame.payload = {0xCA, 0xFE};

    // Compute the correct CRC
    // Reconstruct header bytes
    uint16_t fl = frame.header.payload_length;
    uint16_t seq = frame.header.sequence_number;
    std::vector<uint8_t> header_bytes(5);
    header_bytes[0] = static_cast<uint8_t>((fl >> 8) & 0xFF);
    header_bytes[1] = static_cast<uint8_t>(fl & 0xFF);
    header_bytes[2] = static_cast<uint8_t>(
        ((frame.header.padding_bits & 0x0F) << 4) |
        ((frame.header.fec_enabled ? 1 : 0) << 3) |
        ((static_cast<uint8_t>(frame.header.fec_code_rate) & 0x03) << 1) |
        ((seq >> 11) & 0x01)
    );
    header_bytes[3] = static_cast<uint8_t>((seq >> 3) & 0xFF);
    header_bytes[4] = static_cast<uint8_t>((seq & 0x07) << 5);

    std::vector<uint8_t> crc_data;
    crc_data.insert(crc_data.end(), header_bytes.begin(), header_bytes.end());
    crc_data.insert(crc_data.end(), frame.payload.begin(), frame.payload.end());
    frame.crc = FrameBuilder::compute_crc32(crc_data);

    EXPECT_TRUE(FrameParser::verify_crc(frame));
}

TEST(FrameParserTest, VerifyCRCDetectsCorruption) {
    Frame frame;
    frame.header.payload_length = 1;
    frame.header.padding_bits = 0;
    frame.header.fec_enabled = false;
    frame.header.fec_code_rate = CodeRate::RATE_1_2;
    frame.header.sequence_number = 0;
    frame.payload = {0xFF};
    frame.crc = 0xDEADBEEF;  // wrong CRC

    EXPECT_FALSE(FrameParser::verify_crc(frame));
}

// ===========================================================================
// Build → Parse round trip
// ===========================================================================

TEST(FrameRoundTripTest, BuildThenParseRecoverFields) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto bits = builder.build_frame(payload, 1, true, CodeRate::RATE_1_2);

    // Strip preamble
    std::vector<uint8_t> after_preamble(bits.begin() + preamble.size(), bits.end());

    auto result = FrameParser::parse(after_preamble);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->header.payload_length, 5u);
    EXPECT_EQ(result->header.padding_bits, 1u);
    EXPECT_TRUE(result->header.fec_enabled);
    EXPECT_EQ(result->header.fec_code_rate, CodeRate::RATE_1_2);
    EXPECT_EQ(result->header.sequence_number, 0u);
    EXPECT_EQ(result->payload, payload);
    EXPECT_TRUE(result->crc_valid);
}

TEST(FrameRoundTripTest, MultipleFramesWithIncrementingSeqNum) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    for (uint16_t seq = 0; seq < 10; ++seq) {
        std::vector<uint8_t> payload = {static_cast<uint8_t>(seq), 0xFF};
        auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

        std::vector<uint8_t> after_preamble(bits.begin() + preamble.size(), bits.end());
        auto result = FrameParser::parse(after_preamble);
        ASSERT_TRUE(result.has_value()) << "Failed to parse frame " << seq;
        EXPECT_EQ(result->header.sequence_number, seq);
        EXPECT_EQ(result->payload, payload);
    }
}

TEST(FrameRoundTripTest, EmptyPayloadRoundTrip) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    std::vector<uint8_t> payload;
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    std::vector<uint8_t> after_preamble(bits.begin() + preamble.size(), bits.end());
    auto result = FrameParser::parse(after_preamble);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header.payload_length, 0u);
    EXPECT_TRUE(result->payload.empty());
    EXPECT_TRUE(result->crc_valid);
}

TEST(FrameRoundTripTest, LargePayloadRoundTrip) {
    auto preamble = double_barker();
    FrameBuilder builder(preamble);

    // 1000-byte payload
    std::vector<uint8_t> payload(1000);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    auto bits = builder.build_frame(payload, 0, true, CodeRate::RATE_3_4);

    std::vector<uint8_t> after_preamble(bits.begin() + preamble.size(), bits.end());
    auto result = FrameParser::parse(after_preamble);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header.payload_length, 1000u);
    EXPECT_EQ(result->payload, payload);
    EXPECT_TRUE(result->crc_valid);
}

// ===========================================================================
// FrameSynchronizer tests
// ===========================================================================

TEST(FrameSynchronizerTest, DetectsPreambleAtStart) {
    auto preamble = double_barker();
    FrameSynchronizer sync(preamble);

    // Build a symbol stream: preamble symbols followed by random data
    auto& preamble_syms = sync.preamble_symbols();
    std::vector<std::complex<float>> symbols;
    symbols.insert(symbols.end(), preamble_syms.begin(), preamble_syms.end());
    // Add some random symbols after
    for (int i = 0; i < 50; ++i) {
        symbols.emplace_back(0.5f, 0.3f);
    }

    auto result = sync.detect(symbols);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, preamble.size());  // index after preamble
}

TEST(FrameSynchronizerTest, DetectsPreambleAtOffset) {
    auto preamble = double_barker();
    FrameSynchronizer sync(preamble);

    auto& preamble_syms = sync.preamble_symbols();

    // Insert some noise symbols before the preamble
    const size_t offset = 20;
    std::vector<std::complex<float>> symbols;
    for (size_t i = 0; i < offset; ++i) {
        symbols.emplace_back(0.1f, -0.2f);  // low-energy noise
    }
    symbols.insert(symbols.end(), preamble_syms.begin(), preamble_syms.end());
    // Add trailing symbols
    for (int i = 0; i < 30; ++i) {
        symbols.emplace_back(0.3f, 0.1f);
    }

    auto result = sync.detect(symbols);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, offset + preamble.size());
}

TEST(FrameSynchronizerTest, ReturnsNulloptWhenNoPreamble) {
    auto preamble = double_barker();
    FrameSynchronizer sync(preamble);

    // Random symbols with no preamble pattern
    std::vector<std::complex<float>> symbols;
    for (int i = 0; i < 100; ++i) {
        float angle = static_cast<float>(i) * 0.7f;
        symbols.emplace_back(0.3f * std::cos(angle), 0.3f * std::sin(angle));
    }

    auto result = sync.detect(symbols);
    EXPECT_FALSE(result.has_value());
}

TEST(FrameSynchronizerTest, ReturnsNulloptForEmptyInput) {
    auto preamble = double_barker();
    FrameSynchronizer sync(preamble);

    std::vector<std::complex<float>> symbols;
    auto result = sync.detect(symbols);
    EXPECT_FALSE(result.has_value());
}

TEST(FrameSynchronizerTest, ReturnsNulloptForTooShortInput) {
    auto preamble = double_barker();
    FrameSynchronizer sync(preamble);

    // Input shorter than preamble
    std::vector<std::complex<float>> symbols(5, {1.0f, 0.0f});
    auto result = sync.detect(symbols);
    EXPECT_FALSE(result.has_value());
}

TEST(FrameSynchronizerTest, DetectsScaledPreamble) {
    auto preamble = double_barker();
    FrameSynchronizer sync(preamble);

    auto& preamble_syms = sync.preamble_symbols();

    // Scale the preamble symbols by a constant factor — normalized correlation
    // should still detect it
    std::vector<std::complex<float>> symbols;
    for (const auto& s : preamble_syms) {
        symbols.push_back(s * 3.0f);
    }
    for (int i = 0; i < 20; ++i) {
        symbols.emplace_back(0.1f, 0.1f);
    }

    auto result = sync.detect(symbols);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, preamble.size());
}
