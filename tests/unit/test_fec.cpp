#include "qpsk_b200/fec.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Construction — valid and invalid code rates
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, AcceptsRateOneHalf) {
    EXPECT_NO_THROW({ FecEncoder enc(CodeRate::RATE_1_2); (void)enc; });
}

TEST(FecEncoderTest, AcceptsRateThreeQuarter) {
    EXPECT_NO_THROW({ FecEncoder enc(CodeRate::RATE_3_4); (void)enc; });
}

TEST(FecEncoderTest, RejectsUnsupportedCodeRate) {
    // Cast an invalid value to CodeRate
    auto bad_rate = static_cast<CodeRate>(99);
    EXPECT_THROW({ FecEncoder enc(bad_rate); (void)enc; }, std::invalid_argument);
}

TEST(FecEncoderTest, UnsupportedRateErrorMessage) {
    auto bad_rate = static_cast<CodeRate>(99);
    try {
        FecEncoder enc(bad_rate);
        (void)enc;
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("Unsupported"), std::string::npos)
            << "Error message should mention 'Unsupported': " << msg;
        EXPECT_NE(msg.find("1/2"), std::string::npos)
            << "Error message should mention supported rate 1/2: " << msg;
        EXPECT_NE(msg.find("3/4"), std::string::npos)
            << "Error message should mention supported rate 3/4: " << msg;
    }
}

// ---------------------------------------------------------------------------
// coded_length — rate 1/2
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, CodedLengthRate12_OneByte) {
    FecEncoder enc(CodeRate::RATE_1_2);
    // 1 byte = 8 bits, + 6 tail = 14 input bits
    // Rate 1/2: 14 * 2 = 28 output bits → ceil(28/8) = 4 bytes
    EXPECT_EQ(enc.coded_length(1), 4u);
}

TEST(FecEncoderTest, CodedLengthRate12_TenBytes) {
    FecEncoder enc(CodeRate::RATE_1_2);
    // 10 bytes = 80 bits, + 6 tail = 86 input bits
    // Rate 1/2: 86 * 2 = 172 output bits → ceil(172/8) = 22 bytes
    EXPECT_EQ(enc.coded_length(10), 22u);
}

TEST(FecEncoderTest, CodedLengthRate12_ZeroBytes) {
    FecEncoder enc(CodeRate::RATE_1_2);
    // 0 bytes = 0 bits, + 6 tail = 6 input bits
    // Rate 1/2: 6 * 2 = 12 output bits → ceil(12/8) = 2 bytes
    EXPECT_EQ(enc.coded_length(0), 2u);
}

// ---------------------------------------------------------------------------
// coded_length — rate 3/4
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, CodedLengthRate34_OneByte) {
    FecEncoder enc(CodeRate::RATE_3_4);
    // 1 byte = 8 bits, + 6 tail = 14 input bits
    // Rate 1/2: 14 * 2 = 28 output bits
    // Puncture: 28 / 6 = 4 full periods (24 bits → keep 16), remainder 4 bits
    //   Pattern positions 0..3 = [1,1,0,1] → keep 3
    //   Total kept = 16 + 3 = 19 bits → ceil(19/8) = 3 bytes
    EXPECT_EQ(enc.coded_length(1), 3u);
}

TEST(FecEncoderTest, CodedLengthRate34_TenBytes) {
    FecEncoder enc(CodeRate::RATE_3_4);
    // 10 bytes = 80 bits, + 6 tail = 86 input bits
    // Rate 1/2: 86 * 2 = 172 output bits
    // Puncture: 172 / 6 = 28 full periods (168 bits → keep 112), remainder 4 bits
    //   Pattern positions 0..3 = [1,1,0,1] → keep 3
    //   Total kept = 112 + 3 = 115 bits → ceil(115/8) = 15 bytes
    EXPECT_EQ(enc.coded_length(10), 15u);
}

// ---------------------------------------------------------------------------
// encode — output length matches coded_length
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, EncodeOutputLengthRate12) {
    FecEncoder enc(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0xAB, 0xCD, 0xEF};
    auto coded = enc.encode(data);
    EXPECT_EQ(coded.size(), enc.coded_length(data.size()));
}

TEST(FecEncoderTest, EncodeOutputLengthRate34) {
    FecEncoder enc(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0xAB, 0xCD, 0xEF};
    auto coded = enc.encode(data);
    EXPECT_EQ(coded.size(), enc.coded_length(data.size()));
}

// ---------------------------------------------------------------------------
// encode — empty input
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, EncodeEmptyInputRate12) {
    FecEncoder enc(CodeRate::RATE_1_2);
    std::vector<uint8_t> data;
    auto coded = enc.encode(data);
    // Even with empty input, tail bits produce output
    EXPECT_EQ(coded.size(), enc.coded_length(0));
    EXPECT_GT(coded.size(), 0u);
}

TEST(FecEncoderTest, EncodeEmptyInputRate34) {
    FecEncoder enc(CodeRate::RATE_3_4);
    std::vector<uint8_t> data;
    auto coded = enc.encode(data);
    EXPECT_EQ(coded.size(), enc.coded_length(0));
    EXPECT_GT(coded.size(), 0u);
}

// ---------------------------------------------------------------------------
// encode — known vector test for rate 1/2
// Verify the encoder produces deterministic output for a known input.
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, EncodeRate12_Deterministic) {
    FecEncoder enc(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0x00};  // all zeros
    auto coded1 = enc.encode(data);
    auto coded2 = enc.encode(data);
    // Encoder is stateless between calls — same input → same output
    EXPECT_EQ(coded1, coded2);
}

// ---------------------------------------------------------------------------
// encode — all-zero input should produce all-zero output (rate 1/2)
// With K=7 convolutional code, all-zero input + all-zero tail bits
// means the shift register stays at 0, so all parity outputs are 0.
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, EncodeRate12_AllZeros) {
    FecEncoder enc(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0x00, 0x00};
    auto coded = enc.encode(data);
    for (uint8_t byte : coded) {
        EXPECT_EQ(byte, 0x00) << "All-zero input should produce all-zero output";
    }
}

// ---------------------------------------------------------------------------
// encode — rate 3/4 output is shorter than rate 1/2 for same input
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, Rate34ShorterThanRate12) {
    FecEncoder enc12(CodeRate::RATE_1_2);
    FecEncoder enc34(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    auto coded12 = enc12.encode(data);
    auto coded34 = enc34.encode(data);
    EXPECT_LT(coded34.size(), coded12.size());
}

// ---------------------------------------------------------------------------
// coded_length — consistency across various sizes
// ---------------------------------------------------------------------------

TEST(FecEncoderTest, CodedLengthConsistencyRate12) {
    FecEncoder enc(CodeRate::RATE_1_2);
    for (size_t len = 0; len <= 100; ++len) {
        std::vector<uint8_t> data(len, 0xAA);
        auto coded = enc.encode(data);
        EXPECT_EQ(coded.size(), enc.coded_length(len))
            << "Mismatch at input length " << len;
    }
}

TEST(FecEncoderTest, CodedLengthConsistencyRate34) {
    FecEncoder enc(CodeRate::RATE_3_4);
    for (size_t len = 0; len <= 100; ++len) {
        std::vector<uint8_t> data(len, 0xAA);
        auto coded = enc.encode(data);
        EXPECT_EQ(coded.size(), enc.coded_length(len))
            << "Mismatch at input length " << len;
    }
}

// ===========================================================================
// FecDecoder tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Construction — valid and invalid code rates
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, AcceptsRateOneHalf) {
    EXPECT_NO_THROW({ FecDecoder dec(CodeRate::RATE_1_2); (void)dec; });
}

TEST(FecDecoderTest, AcceptsRateThreeQuarter) {
    EXPECT_NO_THROW({ FecDecoder dec(CodeRate::RATE_3_4); (void)dec; });
}

TEST(FecDecoderTest, RejectsUnsupportedCodeRate) {
    auto bad_rate = static_cast<CodeRate>(99);
    EXPECT_THROW({ FecDecoder dec(bad_rate); (void)dec; }, std::invalid_argument);
}

TEST(FecDecoderTest, UnsupportedRateErrorMessage) {
    auto bad_rate = static_cast<CodeRate>(99);
    try {
        FecDecoder dec(bad_rate);
        (void)dec;
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("Unsupported"), std::string::npos)
            << "Error message should mention 'Unsupported': " << msg;
        EXPECT_NE(msg.find("1/2"), std::string::npos)
            << "Error message should mention supported rate 1/2: " << msg;
        EXPECT_NE(msg.find("3/4"), std::string::npos)
            << "Error message should mention supported rate 3/4: " << msg;
    }
}

// ---------------------------------------------------------------------------
// Encode then decode round-trip — rate 1/2
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, RoundTripRate12_SingleByte) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0xAB};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

TEST(FecDecoderTest, RoundTripRate12_MultipleBytes) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

TEST(FecDecoderTest, RoundTripRate12_AllZeros) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0x00, 0x00, 0x00};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

TEST(FecDecoderTest, RoundTripRate12_AllOnes) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0xFF, 0xFF, 0xFF};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

// ---------------------------------------------------------------------------
// Encode then decode round-trip — rate 3/4
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, RoundTripRate34_SingleByte) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0xAB};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

TEST(FecDecoderTest, RoundTripRate34_MultipleBytes) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

TEST(FecDecoderTest, RoundTripRate34_AllZeros) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0x00, 0x00, 0x00};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

TEST(FecDecoderTest, RoundTripRate34_AllOnes) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0xFF, 0xFF, 0xFF};
    auto coded = enc.encode(data);
    auto decoded = dec.decode(coded, data.size());
    EXPECT_EQ(decoded, data);
}

// ---------------------------------------------------------------------------
// Round-trip with various payload sizes
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, RoundTripRate12_VariousSizes) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    for (size_t len = 1; len <= 64; ++len) {
        std::vector<uint8_t> data(len);
        // Fill with a pattern based on length
        for (size_t i = 0; i < len; ++i) {
            data[i] = static_cast<uint8_t>((i * 37 + len) & 0xFF);
        }
        auto coded = enc.encode(data);
        auto decoded = dec.decode(coded, data.size());
        EXPECT_EQ(decoded, data) << "Round-trip failed for length " << len;
    }
}

TEST(FecDecoderTest, RoundTripRate34_VariousSizes) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    for (size_t len = 1; len <= 64; ++len) {
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i) {
            data[i] = static_cast<uint8_t>((i * 37 + len) & 0xFF);
        }
        auto coded = enc.encode(data);
        auto decoded = dec.decode(coded, data.size());
        EXPECT_EQ(decoded, data) << "Round-trip failed for length " << len;
    }
}

// ---------------------------------------------------------------------------
// get_corrected_errors() returns 0 for clean (error-free) data
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, CorrectedErrorsZeroForCleanDataRate12) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0xCA, 0xFE, 0xBA, 0xBE};
    auto coded = enc.encode(data);
    dec.decode(coded, data.size());
    EXPECT_EQ(dec.get_corrected_errors(), 0u);
}

TEST(FecDecoderTest, CorrectedErrorsZeroForCleanDataRate34) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    std::vector<uint8_t> data = {0xCA, 0xFE, 0xBA, 0xBE};
    auto coded = enc.encode(data);
    dec.decode(coded, data.size());
    EXPECT_EQ(dec.get_corrected_errors(), 0u);
}

// ---------------------------------------------------------------------------
// Error count accumulates across multiple decode calls
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, ErrorCountAccumulatesAcrossCalls) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    std::vector<uint8_t> data = {0x55, 0xAA};
    auto coded = enc.encode(data);

    // Two clean decodes — errors should stay at 0
    dec.decode(coded, data.size());
    dec.decode(coded, data.size());
    EXPECT_EQ(dec.get_corrected_errors(), 0u);
}

// ---------------------------------------------------------------------------
// reset_error_count() clears the counter
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, ResetErrorCount) {
    FecDecoder dec(CodeRate::RATE_1_2);
    EXPECT_EQ(dec.get_corrected_errors(), 0u);
    dec.reset_error_count();
    EXPECT_EQ(dec.get_corrected_errors(), 0u);
}

// ---------------------------------------------------------------------------
// Decode output length matches original input length
// ---------------------------------------------------------------------------

TEST(FecDecoderTest, OutputLengthMatchesOriginalRate12) {
    FecEncoder enc(CodeRate::RATE_1_2);
    FecDecoder dec(CodeRate::RATE_1_2);
    for (size_t len = 1; len <= 32; ++len) {
        std::vector<uint8_t> data(len, 0x42);
        auto coded = enc.encode(data);
        auto decoded = dec.decode(coded, len);
        EXPECT_EQ(decoded.size(), len) << "Output length mismatch for input length " << len;
    }
}

TEST(FecDecoderTest, OutputLengthMatchesOriginalRate34) {
    FecEncoder enc(CodeRate::RATE_3_4);
    FecDecoder dec(CodeRate::RATE_3_4);
    for (size_t len = 1; len <= 32; ++len) {
        std::vector<uint8_t> data(len, 0x42);
        auto coded = enc.encode(data);
        auto decoded = dec.decode(coded, len);
        EXPECT_EQ(decoded.size(), len) << "Output length mismatch for input length " << len;
    }
}
