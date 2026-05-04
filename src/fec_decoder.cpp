#include "qpsk_b200/fec.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// parity  —  compute parity of (value & mask), i.e. XOR of selected bits
// ---------------------------------------------------------------------------
uint8_t FecDecoder::parity(uint8_t value, uint8_t mask) {
    uint8_t v = value & mask;
    v ^= (v >> 4);
    v ^= (v >> 2);
    v ^= (v >> 1);
    return v & 1;
}

// ---------------------------------------------------------------------------
// Constructor  —  validate code rate
// ---------------------------------------------------------------------------
FecDecoder::FecDecoder(CodeRate rate) : code_rate_(rate) {
    if (rate != CodeRate::RATE_1_2 && rate != CodeRate::RATE_3_4) {
        throw std::invalid_argument(
            "Unsupported FEC code rate. Supported values: 1/2, 3/4");
    }
}

// ---------------------------------------------------------------------------
// get_corrected_errors / reset_error_count
// ---------------------------------------------------------------------------
uint64_t FecDecoder::get_corrected_errors() const {
    return corrected_errors_;
}

void FecDecoder::reset_error_count() {
    corrected_errors_ = 0;
}

// ---------------------------------------------------------------------------
// decode  —  Viterbi decoding with optional depuncturing
// ---------------------------------------------------------------------------
std::vector<uint8_t> FecDecoder::decode(const std::vector<uint8_t>& coded_data,
                                        size_t original_byte_length) {
    const size_t original_bits = original_byte_length * 8;
    const size_t total_input_bits = original_bits + TAIL_BITS;
    const size_t rate_half_bits = 2 * total_input_bits;

    // Compute the exact number of coded bits (before byte-packing padding)
    size_t actual_coded_bits = 0;
    if (code_rate_ == CodeRate::RATE_1_2) {
        actual_coded_bits = rate_half_bits;
    } else {
        const size_t full_periods = rate_half_bits / PUNCTURE_PERIOD;
        const size_t remainder    = rate_half_bits % PUNCTURE_PERIOD;
        actual_coded_bits = full_periods * 4;
        for (size_t i = 0; i < remainder; ++i) {
            actual_coded_bits += PUNCTURE_PATTERN[i];
        }
    }

    // --- Step 1: Unpack coded bytes to bits, trimming to actual coded bits ---
    std::vector<uint8_t> coded_bits;
    coded_bits.reserve(actual_coded_bits);
    for (uint8_t byte : coded_data) {
        for (int b = 7; b >= 0; --b) {
            coded_bits.push_back((byte >> b) & 1);
            if (coded_bits.size() >= actual_coded_bits) break;
        }
        if (coded_bits.size() >= actual_coded_bits) break;
    }

    // --- Step 2: For rate 3/4, depuncture ---
    // Insert ERASURE markers at punctured positions.
    std::vector<uint8_t> depunctured;

    if (code_rate_ == CodeRate::RATE_1_2) {
        depunctured = std::move(coded_bits);
    } else {
        depunctured.reserve(rate_half_bits);
        size_t coded_idx = 0;
        for (size_t i = 0; i < rate_half_bits; ++i) {
            if (PUNCTURE_PATTERN[i % PUNCTURE_PERIOD] == 1) {
                if (coded_idx < coded_bits.size()) {
                    depunctured.push_back(coded_bits[coded_idx++]);
                } else {
                    depunctured.push_back(ERASURE);
                }
            } else {
                depunctured.push_back(ERASURE);
            }
        }
    }

    // --- Step 3: Viterbi decoding ---
    const size_t num_steps = depunctured.size() / 2;

    // Use the full 7-bit shift register as state (128 states).
    // This exactly mirrors the encoder's shift register convention:
    //   new_reg = (old_reg >> 1) | (input_bit << 6)
    static constexpr int FULL_STATES = 1 << K;  // 128

    // Precompute state transition table
    struct Transition {
        uint8_t next_state;
        uint8_t out0;
        uint8_t out1;
    };
    Transition trans[FULL_STATES][2];

    for (int s = 0; s < FULL_STATES; ++s) {
        for (int b = 0; b < 2; ++b) {
            uint8_t new_reg = static_cast<uint8_t>(
                ((s >> 1) | (b << (K - 1))) & 0x7F);
            trans[s][b].next_state = new_reg;
            trans[s][b].out0 = parity(new_reg, GEN_0);
            trans[s][b].out1 = parity(new_reg, GEN_1);
        }
    }

    // Path metrics
    constexpr uint32_t INF_METRIC = 0xFFFFFF;
    std::vector<uint32_t> prev_metrics(FULL_STATES, INF_METRIC);
    std::vector<uint32_t> curr_metrics(FULL_STATES, INF_METRIC);
    prev_metrics[0] = 0;  // encoder starts in state 0

    // Survivor path storage: survivor[step * FULL_STATES + state] = prev_state
    std::vector<uint8_t> survivor(num_steps * FULL_STATES, 0);

    for (size_t step = 0; step < num_steps; ++step) {
        uint8_t rx0 = depunctured[step * 2];
        uint8_t rx1 = depunctured[step * 2 + 1];

        std::fill(curr_metrics.begin(), curr_metrics.end(), INF_METRIC);

        for (int s = 0; s < FULL_STATES; ++s) {
            if (prev_metrics[s] >= INF_METRIC) continue;

            for (int b = 0; b < 2; ++b) {
                const auto& t = trans[s][b];

                // Branch metric: Hamming distance; ERASURE = 0 contribution
                uint32_t bm = 0;
                if (rx0 != ERASURE) {
                    bm += (rx0 != t.out0) ? 1u : 0u;
                }
                if (rx1 != ERASURE) {
                    bm += (rx1 != t.out1) ? 1u : 0u;
                }

                uint32_t candidate = prev_metrics[s] + bm;
                if (candidate < curr_metrics[t.next_state]) {
                    curr_metrics[t.next_state] = candidate;
                    survivor[step * FULL_STATES + t.next_state] =
                        static_cast<uint8_t>(s);
                }
            }
        }

        std::swap(prev_metrics, curr_metrics);
    }

    // --- Step 4: Traceback from the best final state ---
    uint8_t state = 0;
    {
        uint32_t best = prev_metrics[0];
        for (int s = 1; s < FULL_STATES; ++s) {
            if (prev_metrics[s] < best) {
                best = prev_metrics[s];
                state = static_cast<uint8_t>(s);
            }
        }
    }

    // Trace back to recover decoded bits.
    // The input bit is at position K-1 (bit 6) of the current state,
    // since: new_reg = (prev_reg >> 1) | (b << 6).
    std::vector<uint8_t> decoded_bits(num_steps);
    for (size_t step = num_steps; step > 0; --step) {
        uint8_t prev_state = survivor[(step - 1) * FULL_STATES + state];
        decoded_bits[step - 1] = (state >> (K - 1)) & 1;
        state = prev_state;
    }

    // --- Step 5: Remove tail bits and pack into bytes ---
    std::vector<uint8_t> output;
    output.reserve(original_byte_length);

    for (size_t i = 0; i < original_bits && i < decoded_bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int b = 0; b < 8 && (i + b) < decoded_bits.size() &&
                                  (i + b) < original_bits; ++b) {
            byte = static_cast<uint8_t>((byte << 1) | decoded_bits[i + b]);
        }
        output.push_back(byte);
    }

    // --- Step 6: Count corrected errors by re-encoding and comparing ---
    FecEncoder encoder(code_rate_);
    auto re_encoded = encoder.encode(output);

    size_t compare_bytes = std::min(re_encoded.size(), coded_data.size());
    uint64_t errors = 0;
    for (size_t i = 0; i < compare_bytes; ++i) {
        uint8_t diff = re_encoded[i] ^ coded_data[i];
        while (diff) {
            errors += diff & 1;
            diff >>= 1;
        }
    }
    corrected_errors_ += errors;

    return output;
}

} // namespace qpsk_b200
