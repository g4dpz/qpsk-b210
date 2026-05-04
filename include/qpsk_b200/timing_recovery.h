#ifndef QPSK_B200_TIMING_RECOVERY_H
#define QPSK_B200_TIMING_RECOVERY_H

#include <complex>
#include <vector>

namespace qpsk_b200 {

/// Mueller-Müller timing recovery for QPSK.
///
/// Operates on carrier-corrected samples at sps samples/symbol and outputs
/// one sample per symbol at the optimal decision instant.  Uses a
/// proportional-integral loop filter to track the timing offset.
///
/// Maps to Requirement 5.4.
class TimingRecovery {
    double mu_;            ///< fractional timing offset (0 to 1)
    double gain_mu_;       ///< proportional gain
    double gain_omega_;    ///< integral gain
    double omega_;         ///< samples per symbol (tracks actual)
    int    sps_;           ///< nominal samples per symbol

    std::complex<float> prev_sample_;    ///< previous interpolated sample
    std::complex<float> prev_decision_;  ///< previous hard decision

public:
    /// Construct a TimingRecovery block.
    ///
    /// @param sps      nominal samples per symbol (default 4)
    /// @param gain_mu  proportional gain for the timing loop (default 0.01)
    explicit TimingRecovery(int sps = 4, double gain_mu = 0.01);

    /// Recover one symbol per sps samples from the input.
    ///
    /// Uses linear interpolation between samples and the Mueller-Müller
    /// timing error detector to find the optimal sampling point.
    ///
    /// @return Recovered symbol-rate samples.
    std::vector<std::complex<float>>
    recover(const std::vector<std::complex<float>>& samples);

    /// Return the current fractional timing offset.
    double get_timing_offset() const;

    /// Reset internal state.
    void reset();
};

} // namespace qpsk_b200

#endif // QPSK_B200_TIMING_RECOVERY_H
