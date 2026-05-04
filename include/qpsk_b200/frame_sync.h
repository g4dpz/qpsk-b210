#ifndef QPSK_B200_FRAME_SYNC_H
#define QPSK_B200_FRAME_SYNC_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace qpsk_b200 {

/// Detects frame boundaries by correlating the incoming symbol stream
/// against the known preamble symbol pattern.
///
/// The preamble bits are BPSK-mapped to symbols: 0 → +1.0, 1 → −1.0
/// (on the I channel only, Q = 0).  A sliding normalized cross-correlation
/// is computed; when it exceeds the configured threshold, the frame start
/// index is returned.
class FrameSynchronizer {
public:
    /// Construct a frame synchronizer.
    ///
    /// @param preamble   Preamble bit pattern (each element 0 or 1).
    /// @param threshold  Normalized correlation threshold (default 0.85).
    FrameSynchronizer(const std::vector<uint8_t>& preamble,
                      float threshold = 0.85f);

    /// Search for the preamble in a symbol stream.
    ///
    /// @param symbols  Complex symbol stream to search.
    /// @return Index of the first symbol AFTER the preamble (i.e., the start
    ///         of the header), or std::nullopt if no preamble is found.
    std::optional<size_t> detect(const std::vector<std::complex<float>>& symbols) const;

    /// Return the BPSK-mapped preamble symbols used for correlation.
    const std::vector<std::complex<float>>& preamble_symbols() const {
        return preamble_symbols_;
    }

private:
    std::vector<std::complex<float>> preamble_symbols_;  ///< BPSK-mapped preamble
    float threshold_;                                     ///< Correlation threshold
};

} // namespace qpsk_b200

#endif // QPSK_B200_FRAME_SYNC_H
