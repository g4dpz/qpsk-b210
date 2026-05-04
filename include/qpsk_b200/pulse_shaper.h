#ifndef QPSK_B200_PULSE_SHAPER_H
#define QPSK_B200_PULSE_SHAPER_H

#include <complex>
#include <vector>

namespace qpsk_b200 {

/// Root-raised-cosine (RRC) pulse shaper and matched filter.
///
/// Generates RRC filter taps at construction time and provides:
///   - shape():          upsample symbols by sps, then convolve with RRC taps
///   - matched_filter(): convolve received samples with RRC taps (no upsampling)
class PulseShaper {
    std::vector<float> rrc_taps_;
    int sps_;

public:
    /// Construct a PulseShaper with the given RRC parameters.
    ///
    /// @param rolloff  RRC roll-off factor α (0 < α ≤ 1.0), default 0.35
    /// @param sps      samples per symbol, default 4
    /// @param num_taps total number of filter taps
    ///                 (typically rrc_span_symbols × sps + 1; default span=6 → 25)
    PulseShaper(double rolloff = 0.35, int sps = 4, int num_taps = 25);

    /// Upsample the symbol stream by sps and convolve with RRC taps.
    ///
    /// 1. Insert (sps − 1) zeros between each symbol.
    /// 2. Convolve the upsampled stream with the RRC taps.
    ///
    /// @return Filtered complex samples of length
    ///         num_symbols × sps + (num_taps − 1).
    std::vector<std::complex<float>>
    shape(const std::vector<std::complex<float>>& symbols) const;

    /// Convolve received samples with RRC taps (no upsampling).
    ///
    /// @return Filtered complex samples of length
    ///         num_samples + (num_taps − 1).
    std::vector<std::complex<float>>
    matched_filter(const std::vector<std::complex<float>>& samples) const;

    /// Access the computed RRC filter taps (useful for testing).
    const std::vector<float>& get_taps() const { return rrc_taps_; }

    /// Return the samples-per-symbol value.
    int get_sps() const { return sps_; }
};

} // namespace qpsk_b200

#endif // QPSK_B200_PULSE_SHAPER_H
