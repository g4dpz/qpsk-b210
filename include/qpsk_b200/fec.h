#ifndef QPSK_B200_FEC_H
#define QPSK_B200_FEC_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "qpsk_b200/types.h"

namespace qpsk_b200 {

/// Forward Error Correction encoder using a rate-1/2 convolutional code
/// (K=7, generators 171/133 octal) with optional puncturing for rate 3/4.
///
/// The encoder appends K−1 = 6 zero tail bits to flush the shift register,
/// then applies the convolutional code.  For rate 3/4, the rate-1/2 output
/// is punctured with pattern [1,1,0,1,1,0] (keep 4 of every 6 bits).
class FecEncoder {
public:
    /// Construct an encoder for the given code rate.
    /// @throws std::invalid_argument if @p rate is not RATE_1_2 or RATE_3_4.
    explicit FecEncoder(CodeRate rate);

    /// Encode a byte vector using the convolutional code.
    ///
    /// 1. Converts input bytes to a bit stream.
    /// 2. Appends K−1 = 6 zero tail bits.
    /// 3. Runs the rate-1/2 convolutional encoder (generators G0=171o, G1=133o).
    /// 4. For rate 3/4, applies puncturing pattern [1,1,0,1,1,0].
    /// 5. Packs output bits back into bytes.
    ///
    /// @return Coded byte vector.
    std::vector<uint8_t> encode(const std::vector<uint8_t>& data);

    /// Compute the coded output length in bytes for a given input length.
    ///
    /// @param input_length  Input size in bytes.
    /// @return Output size in bytes (ceil of output bits / 8).
    size_t coded_length(size_t input_length) const;

private:
    CodeRate code_rate_;

    static constexpr int K = 7;                // constraint length
    static constexpr uint8_t GEN_0 = 0171;     // 171 octal = 0x79
    static constexpr uint8_t GEN_1 = 0133;     // 133 octal = 0x5B
    static constexpr int TAIL_BITS = K - 1;    // 6 tail bits to flush encoder

    /// Puncturing pattern for rate 3/4: applied to pairs (G0, G1) cyclically.
    /// 1 = keep, 0 = discard.
    static constexpr uint8_t PUNCTURE_PATTERN[] = {1, 1, 0, 1, 1, 0};
    static constexpr int PUNCTURE_PERIOD = 6;

    /// Compute parity of (value & mask) — returns 0 or 1.
    static uint8_t parity(uint8_t value, uint8_t mask);
};

/// Forward Error Correction decoder using the Viterbi algorithm for
/// maximum-likelihood decoding of the convolutional code (K=7, generators
/// 171/133 octal).  For rate 3/4, depuncturing inserts erasure markers at
/// punctured positions before Viterbi decoding.  Tracks corrected bit
/// errors per frame for diagnostic logging.
class FecDecoder {
public:
    /// Construct a decoder for the given code rate.
    /// @throws std::invalid_argument if @p rate is not RATE_1_2 or RATE_3_4.
    explicit FecDecoder(CodeRate rate);

    /// Decode coded data using the Viterbi algorithm.
    ///
    /// 1. Unpacks coded bytes to bits.
    /// 2. For rate 3/4: depunctures by inserting erasure markers (value 2)
    ///    at punctured positions using pattern [1,1,0,1,1,0].
    /// 3. Runs hard-decision Viterbi decoding with traceback.
    /// 4. Extracts decoded bits, removes tail bits, packs into bytes.
    /// 5. Counts corrected errors by re-encoding and comparing.
    ///
    /// @param coded_data  The coded byte vector (output of FecEncoder::encode()).
    /// @param original_byte_length  The original (pre-encoding) payload length
    ///                              in bytes, needed to determine output size.
    /// @return Decoded byte vector of length @p original_byte_length.
    std::vector<uint8_t> decode(const std::vector<uint8_t>& coded_data,
                                size_t original_byte_length);

    /// Return the cumulative number of corrected bit errors across all
    /// decode() calls since construction or the last reset_error_count().
    uint64_t get_corrected_errors() const;

    /// Reset the corrected-error counter to zero.
    void reset_error_count();

private:
    CodeRate code_rate_;

    static constexpr int K = 7;
    static constexpr int NUM_STATES = 1 << (K - 1);  // 64 states
    static constexpr uint8_t GEN_0 = 0171;            // 171 octal = 0x79
    static constexpr uint8_t GEN_1 = 0133;            // 133 octal = 0x5B
    static constexpr int TAIL_BITS = K - 1;            // 6

    /// Puncturing pattern for rate 3/4 (same as encoder).
    static constexpr uint8_t PUNCTURE_PATTERN[] = {1, 1, 0, 1, 1, 0};
    static constexpr int PUNCTURE_PERIOD = 6;

    /// Erasure marker value — indicates a depunctured (unknown) bit.
    static constexpr uint8_t ERASURE = 2;

    uint64_t corrected_errors_ = 0;

    /// Compute parity of (value & mask) — returns 0 or 1.
    static uint8_t parity(uint8_t value, uint8_t mask);
};

} // namespace qpsk_b200

#endif // QPSK_B200_FEC_H
