#include "qpsk_b200/fec.h"

#include <stdexcept>
#include <string>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// parity  —  compute parity of (value & mask), i.e. XOR of selected bits
// ---------------------------------------------------------------------------
uint8_t FecEncoder::parity(uint8_t value, uint8_t mask) {
    uint8_t v = value & mask;
    v ^= (v >> 4);
    v ^= (v >> 2);
    v ^= (v >> 1);
    return v & 1;
}

// ---------------------------------------------------------------------------
// Constructor  —  validate code rate
// ---------------------------------------------------------------------------
FecEncoder::FecEncoder(CodeRate rate) : code_rate_(rate) {
    if (rate != CodeRate::RATE_1_2 && rate != CodeRate::RATE_3_4) {
        throw std::invalid_argument(
            "Unsupported FEC code rate. Supported values: 1/2, 3/4");
    }
}

// ---------------------------------------------------------------------------
// coded_length  —  output byte count for a given input byte count
// ---------------------------------------------------------------------------
size_t FecEncoder::coded_length(size_t input_length) const {
    const size_t input_bits = input_length * 8;
    const size_t total_input_bits = input_bits + TAIL_BITS;

    // Rate 1/2: 2 output bits per input bit
    const size_t rate_half_bits = 2 * total_input_bits;

    if (code_rate_ == CodeRate::RATE_1_2) {
        return (rate_half_bits + 7) / 8;
    }

    // Rate 3/4: puncture keeps 4 out of every 6 rate-1/2 output bits.
    // Number of complete puncturing periods (each period = 6 rate-1/2 bits)
    const size_t full_periods = rate_half_bits / PUNCTURE_PERIOD;
    const size_t remainder    = rate_half_bits % PUNCTURE_PERIOD;

    // Each full period keeps 4 bits (pattern [1,1,0,1,1,0] has 4 ones)
    size_t punctured_bits = full_periods * 4;

    // Count kept bits in the partial period
    for (size_t i = 0; i < remainder; ++i) {
        punctured_bits += PUNCTURE_PATTERN[i];
    }

    return (punctured_bits + 7) / 8;
}

// ---------------------------------------------------------------------------
// encode  —  convolutional encoding with optional puncturing
// ---------------------------------------------------------------------------
std::vector<uint8_t> FecEncoder::encode(const std::vector<uint8_t>& data) {
    // --- Step 1: Convert input bytes to bits ---
    std::vector<uint8_t> input_bits;
    input_bits.reserve(data.size() * 8 + TAIL_BITS);

    for (uint8_t byte : data) {
        for (int b = 7; b >= 0; --b) {
            input_bits.push_back((byte >> b) & 1);
        }
    }

    // --- Step 2: Append K-1 = 6 zero tail bits to flush the encoder ---
    for (int i = 0; i < TAIL_BITS; ++i) {
        input_bits.push_back(0);
    }

    // --- Step 3: Run rate-1/2 convolutional encoder ---
    // Shift register holds the K most recent input bits.
    // New bits enter at the MSB (bit position K-1).
    uint8_t shift_reg = 0;
    std::vector<uint8_t> rate_half_output;
    rate_half_output.reserve(input_bits.size() * 2);

    for (uint8_t bit : input_bits) {
        // Shift new bit into the register (MSB position)
        shift_reg = static_cast<uint8_t>(((shift_reg >> 1) | (bit << (K - 1))) & 0x7F);

        // Output bit 0: parity of (shift_reg & G0)
        rate_half_output.push_back(parity(shift_reg, GEN_0));
        // Output bit 1: parity of (shift_reg & G1)
        rate_half_output.push_back(parity(shift_reg, GEN_1));
    }

    // --- Step 4: Apply puncturing for rate 3/4 ---
    std::vector<uint8_t> coded_bits;
    if (code_rate_ == CodeRate::RATE_1_2) {
        coded_bits = std::move(rate_half_output);
    } else {
        // Rate 3/4: apply puncturing pattern [1,1,0,1,1,0]
        coded_bits.reserve((rate_half_output.size() * 4 + 5) / 6);
        for (size_t i = 0; i < rate_half_output.size(); ++i) {
            if (PUNCTURE_PATTERN[i % PUNCTURE_PERIOD] == 1) {
                coded_bits.push_back(rate_half_output[i]);
            }
        }
    }

    // --- Step 5: Pack output bits into bytes ---
    std::vector<uint8_t> output;
    output.reserve((coded_bits.size() + 7) / 8);

    uint8_t current_byte = 0;
    int bit_count = 0;

    for (uint8_t bit : coded_bits) {
        current_byte = static_cast<uint8_t>((current_byte << 1) | bit);
        ++bit_count;
        if (bit_count == 8) {
            output.push_back(current_byte);
            current_byte = 0;
            bit_count = 0;
        }
    }

    // Flush any remaining bits (left-aligned in the final byte)
    if (bit_count > 0) {
        current_byte = static_cast<uint8_t>(current_byte << (8 - bit_count));
        output.push_back(current_byte);
    }

    return output;
}

} // namespace qpsk_b200
