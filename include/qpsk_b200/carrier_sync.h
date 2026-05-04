#ifndef QPSK_B200_CARRIER_SYNC_H
#define QPSK_B200_CARRIER_SYNC_H

#include <complex>
#include <vector>

namespace qpsk_b200 {

/// Second-order Costas loop carrier synchronizer for QPSK.
///
/// Estimates and corrects carrier frequency offset and phase offset in the
/// received baseband signal.  The loop bandwidth controls the trade-off
/// between acquisition speed and steady-state noise.
///
/// Phase error detector (QPSK decision-directed):
///   error = sign(Q) · I − sign(I) · Q
///
/// Maps to Requirements 5.2, 5.3.
class CarrierSynchronizer {
    double phase_;   ///< current phase estimate (radians)
    double freq_;    ///< current frequency estimate (radians/sample)
    double alpha_;   ///< proportional gain
    double beta_;    ///< integral gain

public:
    /// Construct a CarrierSynchronizer with the given loop bandwidth.
    ///
    /// @param loop_bw  normalised loop bandwidth (fraction of symbol rate),
    ///                 default 0.01
    explicit CarrierSynchronizer(double loop_bw = 0.01);

    /// Correct carrier offset on a block of samples.
    ///
    /// For each input sample the method:
    ///   1. Rotates the sample by the current phase estimate.
    ///   2. Computes the QPSK phase error.
    ///   3. Updates frequency and phase via the loop filter.
    ///
    /// @return Carrier-corrected samples (same length as input).
    std::vector<std::complex<float>>
    synchronize(const std::vector<std::complex<float>>& samples);

    /// Return the current frequency offset estimate (radians/sample).
    double get_freq_offset() const;

    /// Return the current phase offset estimate (radians).
    double get_phase_offset() const;

    /// Reset internal state to zero.
    void reset();
};

} // namespace qpsk_b200

#endif // QPSK_B200_CARRIER_SYNC_H
