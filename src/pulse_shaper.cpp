#include "qpsk_b200/pulse_shaper.h"

#include <cmath>
#include <numeric>
#include <stdexcept>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// Helper: compute a single RRC tap value at normalised time index t/T
//
// Standard RRC impulse response:
//
//   h(t) = sin(π·t/T·(1−α)) + 4·α·t/T·cos(π·t/T·(1+α))
//          ─────────────────────────────────────────────────
//                 π·t/T·(1 − (4·α·t/T)²)
//
// Special cases:
//   t = 0:          h(0) = 1 − α + 4α/π
//   t = ±T/(4α):    h    = (α/√2)·[(1+2/π)·sin(π/(4α)) + (1−2/π)·cos(π/(4α))]
// ---------------------------------------------------------------------------
static double rrc_tap(double t_over_T, double alpha) {
    constexpr double PI = M_PI;

    // Special case: t = 0
    if (std::abs(t_over_T) < 1e-12) {
        return 1.0 - alpha + 4.0 * alpha / PI;
    }

    // Special case: t = ±T/(4α)  →  |t/T| = 1/(4α)
    if (alpha > 1e-12) {
        double critical = 1.0 / (4.0 * alpha);
        if (std::abs(std::abs(t_over_T) - critical) < 1e-8) {
            return (alpha / std::sqrt(2.0)) *
                   ((1.0 + 2.0 / PI) * std::sin(PI / (4.0 * alpha)) +
                    (1.0 - 2.0 / PI) * std::cos(PI / (4.0 * alpha)));
        }
    }

    // General case
    double numerator = std::sin(PI * t_over_T * (1.0 - alpha)) +
                       4.0 * alpha * t_over_T *
                           std::cos(PI * t_over_T * (1.0 + alpha));

    double denominator = PI * t_over_T *
                         (1.0 - std::pow(4.0 * alpha * t_over_T, 2.0));

    // Guard against near-zero denominator (shouldn't happen after special cases,
    // but be safe with floating-point)
    if (std::abs(denominator) < 1e-15) {
        // Use L'Hôpital / limiting value — fall back to numerical nudge
        double eps = 1e-6;
        return 0.5 * (rrc_tap(t_over_T + eps, alpha) +
                       rrc_tap(t_over_T - eps, alpha));
    }

    return numerator / denominator;
}

// ---------------------------------------------------------------------------
// Constructor — generate RRC taps and normalise to unit energy
// ---------------------------------------------------------------------------
PulseShaper::PulseShaper(double rolloff, int sps, int num_taps)
    : sps_(sps) {
    if (rolloff <= 0.0 || rolloff > 1.0) {
        throw std::invalid_argument(
            "PulseShaper: rolloff must be in (0, 1.0], got " +
            std::to_string(rolloff));
    }
    if (sps < 1) {
        throw std::invalid_argument(
            "PulseShaper: sps must be >= 1, got " + std::to_string(sps));
    }
    if (num_taps < 1) {
        throw std::invalid_argument(
            "PulseShaper: num_taps must be >= 1, got " +
            std::to_string(num_taps));
    }

    rrc_taps_.resize(static_cast<size_t>(num_taps));

    // The filter is centred at tap index (num_taps − 1) / 2.
    // Each tap corresponds to t/T = (i − centre) / sps.
    double centre = static_cast<double>(num_taps - 1) / 2.0;

    for (int i = 0; i < num_taps; ++i) {
        double t_over_T = (static_cast<double>(i) - centre) / static_cast<double>(sps);
        rrc_taps_[static_cast<size_t>(i)] = static_cast<float>(rrc_tap(t_over_T, rolloff));
    }

    // Normalise to unit energy: sum of squares = 1
    double energy = 0.0;
    for (float tap : rrc_taps_) {
        energy += static_cast<double>(tap) * static_cast<double>(tap);
    }
    if (energy > 0.0) {
        float scale = static_cast<float>(1.0 / std::sqrt(energy));
        for (float& tap : rrc_taps_) {
            tap *= scale;
        }
    }
}

// ---------------------------------------------------------------------------
// shape — upsample by sps, then convolve with RRC taps
// ---------------------------------------------------------------------------
std::vector<std::complex<float>>
PulseShaper::shape(const std::vector<std::complex<float>>& symbols) const {
    if (symbols.empty()) {
        return {};
    }

    const size_t num_symbols = symbols.size();
    const size_t upsampled_len = num_symbols * static_cast<size_t>(sps_);
    const size_t filter_len = rrc_taps_.size();
    const size_t output_len = upsampled_len + filter_len - 1;

    // Build upsampled signal: insert (sps − 1) zeros between each symbol
    std::vector<std::complex<float>> upsampled(upsampled_len, {0.0f, 0.0f});
    for (size_t i = 0; i < num_symbols; ++i) {
        upsampled[i * static_cast<size_t>(sps_)] = symbols[i];
    }

    // Convolve upsampled signal with RRC taps
    std::vector<std::complex<float>> output(output_len, {0.0f, 0.0f});
    for (size_t n = 0; n < output_len; ++n) {
        std::complex<float> sum{0.0f, 0.0f};
        for (size_t k = 0; k < filter_len; ++k) {
            if (n >= k && (n - k) < upsampled_len) {
                sum += upsampled[n - k] * rrc_taps_[k];
            }
        }
        output[n] = sum;
    }

    return output;
}

// ---------------------------------------------------------------------------
// matched_filter — convolve received samples with RRC taps (no upsampling)
// ---------------------------------------------------------------------------
std::vector<std::complex<float>>
PulseShaper::matched_filter(
    const std::vector<std::complex<float>>& samples) const {
    if (samples.empty()) {
        return {};
    }

    const size_t num_samples = samples.size();
    const size_t filter_len = rrc_taps_.size();
    const size_t output_len = num_samples + filter_len - 1;

    std::vector<std::complex<float>> output(output_len, {0.0f, 0.0f});
    for (size_t n = 0; n < output_len; ++n) {
        std::complex<float> sum{0.0f, 0.0f};
        for (size_t k = 0; k < filter_len; ++k) {
            if (n >= k && (n - k) < num_samples) {
                sum += samples[n - k] * rrc_taps_[k];
            }
        }
        output[n] = sum;
    }

    return output;
}

} // namespace qpsk_b200
