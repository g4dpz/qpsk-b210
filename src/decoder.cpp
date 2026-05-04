#include "qpsk_b200/decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
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

/// BPSK-map preamble bits to reference symbols: 0 → +1.0, 1 → −1.0 (I only).
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

/// Estimate the residual phase rotation by correlating received preamble
/// symbols with the known reference preamble symbols.
std::complex<float> estimate_phase_correction(
    const std::vector<std::complex<float>>& received_preamble,
    const std::vector<std::complex<float>>& reference_preamble) {

    std::complex<float> corr(0.0f, 0.0f);
    size_t len = std::min(received_preamble.size(), reference_preamble.size());
    for (size_t i = 0; i < len; ++i) {
        corr += received_preamble[i] * std::conj(reference_preamble[i]);
    }

    float mag = std::abs(corr);
    if (mag < 1e-10f) {
        return {1.0f, 0.0f};
    }

    return std::conj(corr) / mag;
}

/// Extract N bits from a bit vector starting at offset, MSB first.
uint32_t extract_bits_from_vec(const std::vector<uint8_t>& bits,
                               size_t offset, size_t count) {
    uint32_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        if (offset + i < bits.size()) {
            value = (value << 1) | (bits[offset + i] & 1u);
        }
    }
    return value;
}

/// Try to decode a frame from a symbol stream.
/// Returns the decoded payload if successful, or nullopt.
/// This is the core decode logic shared by all recovery paths.
std::optional<std::vector<uint8_t>>
try_decode_frame(const std::vector<std::complex<float>>& recovered,
                 const std::vector<uint8_t>& doubled_preamble,
                 const FrameSynchronizer& frame_sync,
                 FecDecoder& fec_decoder,
                 RxDiagnostics& diag) {

    const size_t preamble_len = doubled_preamble.size();

    // Frame synchronization
    auto frame_start = frame_sync.detect(recovered);
    if (!frame_start) {
        return std::nullopt;
    }

    // Phase correction from preamble
    std::complex<float> phase_correction = {1.0f, 0.0f};
    if (*frame_start >= preamble_len) {
        size_t preamble_start = *frame_start - preamble_len;
        std::vector<std::complex<float>> received_preamble(
            recovered.begin() + static_cast<long>(preamble_start),
            recovered.begin() + static_cast<long>(*frame_start));

        auto reference_preamble = bpsk_map_preamble(doubled_preamble);
        phase_correction = estimate_phase_correction(received_preamble,
                                                      reference_preamble);
    }

    // Extract and correct data symbols
    std::vector<std::complex<float>> data_symbols(
        recovered.begin() + static_cast<long>(*frame_start),
        recovered.end());

    for (auto& sym : data_symbols) {
        sym *= phase_correction;
    }

    // Parse header to determine frame length
    constexpr size_t HEADER_BITS = 35;
    constexpr size_t CRC_BITS = 32;
    const size_t header_symbols = (HEADER_BITS + 1) / 2;

    if (data_symbols.size() < header_symbols) {
        return std::nullopt;
    }

    std::vector<std::complex<float>> header_syms(
        data_symbols.begin(),
        data_symbols.begin() + static_cast<long>(header_symbols));
    auto header_bits = SymbolMapper::demap(header_syms);

    uint16_t frame_length = static_cast<uint16_t>(
        extract_bits_from_vec(header_bits, 0, 16));
    bool fec_enabled_flag = (extract_bits_from_vec(header_bits, 20, 1) != 0);
    uint8_t fec_rate_val = static_cast<uint8_t>(
        extract_bits_from_vec(header_bits, 21, 2));

    // Determine payload bit count
    size_t payload_bit_count;
    if (fec_enabled_flag) {
        CodeRate rate = static_cast<CodeRate>(fec_rate_val);
        if (rate != CodeRate::RATE_1_2 && rate != CodeRate::RATE_3_4) {
            return std::nullopt;
        }
        FecEncoder temp_encoder(rate);
        size_t coded_bytes = temp_encoder.coded_length(frame_length);
        payload_bit_count = coded_bytes * 8;
    } else {
        payload_bit_count = static_cast<size_t>(frame_length) * 8;
    }

    size_t total_data_bits = HEADER_BITS + payload_bit_count + CRC_BITS;
    size_t total_data_symbols = (total_data_bits + 1) / 2;

    if (data_symbols.size() < total_data_symbols) {
        return std::nullopt;
    }

    // Demap trimmed symbols
    std::vector<std::complex<float>> frame_symbols(
        data_symbols.begin(),
        data_symbols.begin() + static_cast<long>(total_data_symbols));
    auto bits = SymbolMapper::demap(frame_symbols);

    if (bits.size() > total_data_bits) {
        bits.resize(total_data_bits);
    }

    // Parse frame
    // When FEC is enabled, we must parse without CRC verification first,
    // FEC-decode the payload, then verify CRC against the decoded payload.
    // This is because the CRC was computed over header + original (pre-FEC)
    // payload, but the frame contains the FEC-coded payload.
    auto frame_opt = fec_enabled_flag
        ? FrameParser::parse_unchecked(bits)
        : FrameParser::parse(bits);

    if (!frame_opt) {
        return std::nullopt;
    }

    Frame& frame = *frame_opt;

    // FEC decode if enabled (before CRC verification)
    std::vector<uint8_t> decoded_payload;
    if (frame.header.fec_enabled) {
        decoded_payload = fec_decoder.decode(
            frame.payload, frame.header.payload_length);

        diag.fec_errors_corrected = fec_decoder.get_corrected_errors();

        // Now verify CRC over header + decoded (original) payload
        Frame crc_check_frame = frame;
        crc_check_frame.payload = decoded_payload;
        if (!FrameParser::verify_crc(crc_check_frame)) {
            spdlog::warn("Decoder: CRC error after FEC decode on frame seq={}",
                         frame.header.sequence_number);
            return std::nullopt;
        }

        spdlog::debug("Decoder: FEC decoded {} coded bytes -> {} original bytes",
                      frame.payload.size(), decoded_payload.size());
    } else {
        if (!frame.crc_valid) {
            return std::nullopt;
        }
        decoded_payload = frame.payload;
        if (decoded_payload.size() > frame.header.payload_length) {
            decoded_payload.resize(frame.header.payload_length);
        }
    }

    diag.frames_received++;

    spdlog::debug("Decoder: frame seq={} received, payload_length={}, "
                  "fec_enabled={}", frame.header.sequence_number,
                  frame.header.payload_length, frame.header.fec_enabled);

    return decoded_payload;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
Decoder::Decoder(const Config& config)
    : config_(config),
      shaper_(config.rrc_rolloff,
              config.samples_per_symbol,
              config.rrc_span_symbols * config.samples_per_symbol + 1),
      carrier_sync_(0.01),
      timing_recovery_(config.samples_per_symbol, 0.01),
      frame_sync_(make_doubled_preamble(config.preamble)),
      fec_decoder_(config.fec_code_rate) {}

// ---------------------------------------------------------------------------
// decode — full RX pipeline
// ---------------------------------------------------------------------------
std::optional<std::vector<uint8_t>>
Decoder::decode(const std::vector<std::complex<float>>& samples) {
    if (samples.empty()) {
        return std::nullopt;
    }

    const auto doubled_preamble = make_doubled_preamble(config_.preamble);
    const int sps = config_.samples_per_symbol;
    const int num_taps = config_.rrc_span_symbols * sps + 1;

    // --- Step 1: Matched filter ---
    auto filtered = shaper_.matched_filter(samples);

    // --- Recovery strategy ---
    // Try multiple symbol recovery approaches in order of preference:
    //
    // 1. Full DSP chain: carrier sync → timing recovery
    //    (best for real channels with freq/phase/timing offsets)
    //
    // 2. Direct decimation without carrier sync
    //    (best for loopback / clean channels with no impairments)
    //
    // 3. Carrier sync + direct decimation
    //    (for channels with freq offset but no timing offset)
    //
    // Each approach produces a symbol stream that is then passed to
    // try_decode_frame() which handles preamble detection, phase correction,
    // header parsing, demapping, and CRC verification.

    // --- Approach 1: Full DSP chain ---
    {
        auto synced = carrier_sync_.synchronize(filtered);
        diag_.last_freq_offset = carrier_sync_.get_freq_offset();
        diag_.last_phase_offset = carrier_sync_.get_phase_offset();

        auto recovered = timing_recovery_.recover(synced);
        diag_.last_timing_offset = timing_recovery_.get_timing_offset();

        spdlog::debug("Decoder: full DSP chain produced {} symbols", recovered.size());

        auto result = try_decode_frame(recovered, doubled_preamble,
                                        frame_sync_, fec_decoder_, diag_);
        if (result) {
            return result;
        }
    }

    // --- Approach 2: Direct decimation (no carrier sync, no timing recovery) ---
    {
        size_t combined_delay = static_cast<size_t>(num_taps - 1);
        std::vector<std::complex<float>> recovered;
        for (size_t k = 0; combined_delay + k * sps < filtered.size(); ++k) {
            recovered.push_back(filtered[combined_delay + k * sps]);
        }

        spdlog::debug("Decoder: direct decimation produced {} symbols",
                      recovered.size());

        auto result = try_decode_frame(recovered, doubled_preamble,
                                        frame_sync_, fec_decoder_, diag_);
        if (result) {
            return result;
        }
    }

    // --- Approach 3: Carrier sync + direct decimation ---
    {
        // Reset carrier sync for a fresh pass
        carrier_sync_.reset();
        auto synced = carrier_sync_.synchronize(filtered);
        diag_.last_freq_offset = carrier_sync_.get_freq_offset();
        diag_.last_phase_offset = carrier_sync_.get_phase_offset();

        size_t combined_delay = static_cast<size_t>(num_taps - 1);
        std::vector<std::complex<float>> recovered;
        for (size_t k = 0; combined_delay + k * sps < synced.size(); ++k) {
            recovered.push_back(synced[combined_delay + k * sps]);
        }

        spdlog::debug("Decoder: carrier sync + decimation produced {} symbols",
                      recovered.size());

        auto result = try_decode_frame(recovered, doubled_preamble,
                                        frame_sync_, fec_decoder_, diag_);
        if (result) {
            return result;
        }
    }

    // All approaches failed
    diag_.crc_errors++;
    spdlog::warn("Decoder: all recovery approaches failed");
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------
void Decoder::reset() {
    carrier_sync_.reset();
    timing_recovery_.reset();
    diag_ = RxDiagnostics{};
}

} // namespace qpsk_b200
