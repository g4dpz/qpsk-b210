#include "qpsk_b200/frame_sync.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

namespace qpsk_b200 {

FrameSynchronizer::FrameSynchronizer(const std::vector<uint8_t>& preamble,
                                     float threshold)
    : threshold_(threshold)
{
    // BPSK-map preamble bits to symbols: 0 → +1.0, 1 → −1.0 (I channel only)
    preamble_symbols_.reserve(preamble.size());
    for (uint8_t bit : preamble) {
        float val = (bit == 0) ? 1.0f : -1.0f;
        preamble_symbols_.emplace_back(val, 0.0f);
    }
}

std::optional<size_t> FrameSynchronizer::detect(
    const std::vector<std::complex<float>>& symbols) const
{
    const size_t plen = preamble_symbols_.size();
    if (symbols.size() < plen || plen == 0) {
        return std::nullopt;
    }

    // Precompute preamble energy (constant across all positions)
    float preamble_energy = 0.0f;
    for (const auto& s : preamble_symbols_) {
        preamble_energy += std::norm(s);  // |s|^2
    }

    const size_t search_len = symbols.size() - plen + 1;

    for (size_t i = 0; i < search_len; ++i) {
        // Compute cross-correlation and signal energy at this position
        std::complex<float> corr(0.0f, 0.0f);
        float signal_energy = 0.0f;

        for (size_t j = 0; j < plen; ++j) {
            corr += symbols[i + j] * std::conj(preamble_symbols_[j]);
            signal_energy += std::norm(symbols[i + j]);
        }

        // Normalized correlation magnitude
        float denom = std::sqrt(preamble_energy * signal_energy);
        if (denom < 1e-12f) {
            continue;  // avoid division by zero
        }

        float normalized = std::abs(corr) / denom;

        if (normalized >= threshold_) {
            // Return the index of the first symbol AFTER the preamble
            return i + plen;
        }
    }

    return std::nullopt;
}

} // namespace qpsk_b200
