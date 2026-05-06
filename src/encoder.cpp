#include "qpsk_b200/encoder.h"

#include <cstdint>
#include <vector>

#include <spdlog/spdlog.h>

namespace qpsk_b200 {

namespace {

/// Double the preamble: the 13-bit Barker sequence is repeated twice to form
/// the 26-bit preamble used in the frame wire format.
std::vector<uint8_t> make_doubled_preamble(const std::vector<uint8_t>& preamble) {
    std::vector<uint8_t> doubled;
    doubled.reserve(preamble.size() * 2);
    doubled.insert(doubled.end(), preamble.begin(), preamble.end());
    doubled.insert(doubled.end(), preamble.begin(), preamble.end());
    return doubled;
}

/// BPSK-map preamble bits to symbols: 0 → +1.0, 1 → −1.0 (I channel only).
/// This matches the FrameSynchronizer's expectation for preamble correlation.
std::vector<std::complex<float>>
bpsk_map_preamble(const std::vector<uint8_t>& preamble_bits) {
    std::vector<std::complex<float>> symbols;
    symbols.reserve(preamble_bits.size());
    for (uint8_t bit : preamble_bits) {
        float val = (bit == 0) ? 1.0f : -1.0f;
        symbols.emplace_back(val, 0.0f);
    }
    return symbols;
}

/// Generate acquisition sequence: alternating QPSK symbols for carrier and
/// timing recovery convergence. Pattern: (+1+j, +1-j, -1-j, -1+j) / sqrt(2).
/// This provides constant-envelope transitions that exercise all four quadrants,
/// ideal for both Costas loop and Gardner TED lock.
std::vector<std::complex<float>>
generate_acquisition_symbols(int count) {
    static constexpr float S = 0.7071067811865475f; // 1/sqrt(2)
    static const std::complex<float> pattern[4] = {
        { S,  S},   // +1+j / sqrt(2)
        { S, -S},   // +1-j / sqrt(2)
        {-S, -S},   // -1-j / sqrt(2)
        {-S,  S}    // -1+j / sqrt(2)
    };

    std::vector<std::complex<float>> symbols;
    symbols.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        symbols.push_back(pattern[i % 4]);
    }
    return symbols;
}

/// Number of zero-valued tail symbols appended after data to flush the
/// convolutional encoder's shift register.
constexpr int TAIL_SYMBOLS = 6;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Encoder::Encoder(const Config& config)
    : config_(config),
      frame_builder_(make_doubled_preamble(config.preamble)),
      fec_encoder_(config.fec_code_rate),
      shaper_(config.rrc_rolloff,
              config.samples_per_symbol,
              config.rrc_span_symbols * config.samples_per_symbol + 1) {}

// ---------------------------------------------------------------------------
// encode — full TX pipeline
//
// Pipeline:
//   1. Build frame with original payload (CRC covers header + original payload,
//      header stores original payload length).
//   2. If FEC enabled: FEC-encode the original payload bytes, convert coded
//      bytes to bits, and splice them into the frame replacing the original
//      payload bit section.
//   3. Separate preamble bits from data bits (header + payload + CRC).
//   4. BPSK-map preamble bits to symbols (one symbol per bit).
//   5. QPSK-map data bits to symbols (one symbol per dibit).
//   6. Concatenate preamble symbols + data symbols.
//   7. Pulse-shape the combined symbol stream.
//   8. Update diagnostics.
// ---------------------------------------------------------------------------
std::vector<std::complex<float>>
Encoder::encode(const std::vector<uint8_t>& payload) {
    const size_t preamble_bits_count = config_.preamble.size() * 2;

    // --- Step 1: Build frame with original payload ---
    std::vector<uint8_t> frame_bits = frame_builder_.build_frame(
        payload, 0, config_.fec_enabled, config_.fec_code_rate);

    // --- Step 2: If FEC enabled, splice in FEC-coded payload bits ---
    if (config_.fec_enabled) {
        std::vector<uint8_t> coded_bytes = fec_encoder_.encode(payload);

        // Convert coded bytes to bits
        std::vector<uint8_t> coded_bits;
        coded_bits.reserve(coded_bytes.size() * 8);
        for (uint8_t byte : coded_bytes) {
            for (int b = 7; b >= 0; --b) {
                coded_bits.push_back((byte >> b) & 1);
            }
        }

        // Frame layout: [preamble][header (35 bits)][payload bits][CRC (32 bits)]
        constexpr size_t HEADER_BITS = 35;
        constexpr size_t CRC_BITS = 32;
        const size_t payload_start = preamble_bits_count + HEADER_BITS;

        // Build new frame: preamble + header + coded_payload_bits + CRC
        std::vector<uint8_t> new_frame_bits;
        new_frame_bits.reserve(payload_start + coded_bits.size() + CRC_BITS);

        // Copy preamble + header
        new_frame_bits.insert(new_frame_bits.end(),
                              frame_bits.begin(),
                              frame_bits.begin() + static_cast<long>(payload_start));

        // Insert FEC-coded payload bits
        new_frame_bits.insert(new_frame_bits.end(),
                              coded_bits.begin(),
                              coded_bits.end());

        // Copy CRC from original frame (last 32 bits)
        new_frame_bits.insert(new_frame_bits.end(),
                              frame_bits.end() - CRC_BITS,
                              frame_bits.end());

        frame_bits = std::move(new_frame_bits);
    }

    // --- Step 3: Separate preamble from data ---
    std::vector<uint8_t> preamble_bits(
        frame_bits.begin(),
        frame_bits.begin() + static_cast<long>(preamble_bits_count));

    std::vector<uint8_t> data_bits(
        frame_bits.begin() + static_cast<long>(preamble_bits_count),
        frame_bits.end());

    // --- Step 4: BPSK-map preamble ---
    auto preamble_symbols = bpsk_map_preamble(preamble_bits);

    // --- Step 5: QPSK-map data bits ---
    auto [data_symbols, padding] = SymbolMapper::map(data_bits);

    // --- Step 6: Generate acquisition sequence ---
    auto acquisition_symbols = generate_acquisition_symbols(config_.acquisition_symbols);

    // --- Step 7: Generate tail symbols (zero-valued flush for conv encoder) ---
    std::vector<std::complex<float>> tail_symbols(TAIL_SYMBOLS, {0.0f, 0.0f});

    // --- Step 8: Concatenate: [acquisition] [preamble] [data] [tail] ---
    std::vector<std::complex<float>> all_symbols;
    all_symbols.reserve(acquisition_symbols.size() +
                        preamble_symbols.size() +
                        data_symbols.size() +
                        tail_symbols.size());
    all_symbols.insert(all_symbols.end(),
                       acquisition_symbols.begin(), acquisition_symbols.end());
    all_symbols.insert(all_symbols.end(),
                       preamble_symbols.begin(), preamble_symbols.end());
    all_symbols.insert(all_symbols.end(),
                       data_symbols.begin(), data_symbols.end());
    all_symbols.insert(all_symbols.end(),
                       tail_symbols.begin(), tail_symbols.end());

    // --- Step 9: Pulse-shape ---
    auto samples = shaper_.shape(all_symbols);

    // --- Step 10: Update diagnostics ---
    diag_.frames_transmitted++;
    diag_.symbols_transmitted += all_symbols.size();

    spdlog::debug("Encoder: frame {} transmitted, {} symbols "
                  "({} acquisition + {} preamble + {} data + {} tail), "
                  "{} samples",
                  diag_.frames_transmitted, all_symbols.size(),
                  acquisition_symbols.size(),
                  preamble_symbols.size(), data_symbols.size(),
                  tail_symbols.size(),
                  samples.size());

    return samples;
}

} // namespace qpsk_b200
