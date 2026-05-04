#include "qpsk_b200/timing_recovery.h"

#include <cmath>
#include <stdexcept>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// Helper: QPSK hard decision — nearest constellation point
//
// Returns the QPSK constellation point closest to the input sample:
//   (+1,+1)/√2, (−1,+1)/√2, (−1,−1)/√2, (+1,−1)/√2
// Simplified to sign-based selection.
// ---------------------------------------------------------------------------
static inline std::complex<float> qpsk_decision(std::complex<float> s) {
    constexpr float INV_SQRT2 = static_cast<float>(M_SQRT1_2);
    float I = (s.real() >= 0.0f) ? INV_SQRT2 : -INV_SQRT2;
    float Q = (s.imag() >= 0.0f) ? INV_SQRT2 : -INV_SQRT2;
    return {I, Q};
}

// ---------------------------------------------------------------------------
// Helper: linear interpolation between two samples
// ---------------------------------------------------------------------------
static inline std::complex<float> interpolate(
    std::complex<float> a, std::complex<float> b, float mu) {
    return a + mu * (b - a);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TimingRecovery::TimingRecovery(int sps, double gain_mu)
    : mu_(0.0),
      gain_mu_(gain_mu),
      gain_omega_(0.25 * gain_mu * gain_mu),   // standard relation
      omega_(static_cast<double>(sps)),
      sps_(sps),
      prev_sample_(0.0f, 0.0f),
      prev_decision_(0.0f, 0.0f) {
    if (sps < 2) {
        throw std::invalid_argument(
            "TimingRecovery: sps must be >= 2, got " + std::to_string(sps));
    }
    if (gain_mu <= 0.0 || gain_mu >= 1.0) {
        throw std::invalid_argument(
            "TimingRecovery: gain_mu must be in (0, 1), got " +
            std::to_string(gain_mu));
    }
}

// ---------------------------------------------------------------------------
// recover — Mueller-Müller timing recovery
//
// Walks through the input sample buffer, stepping by omega_ + mu_ samples
// at a time.  At each step:
//   1. Linearly interpolate between the two samples surrounding the
//      fractional index to get the current symbol sample.
//   2. Make a hard QPSK decision on the interpolated sample.
//   3. Compute the Mueller-Müller timing error:
//        e = Re{ prev_decision · conj(current_sample)
//              − current_decision · conj(prev_sample) }
//   4. Update mu and omega via the PI loop filter.
//   5. Store current sample/decision as previous for next iteration.
// ---------------------------------------------------------------------------
std::vector<std::complex<float>>
TimingRecovery::recover(const std::vector<std::complex<float>>& samples) {
    if (samples.empty()) {
        return {};
    }

    std::vector<std::complex<float>> out;
    out.reserve(samples.size() / static_cast<size_t>(sps_) + 1);

    const auto n = static_cast<double>(samples.size());
    double index = 0.0;

    while (index + 1.0 < n) {
        // Integer and fractional parts of the current index
        auto i = static_cast<size_t>(index);
        auto frac = static_cast<float>(index - static_cast<double>(i));

        // Bounds check — need at least i+1 for interpolation
        if (i + 1 >= samples.size()) {
            break;
        }

        // 1. Linear interpolation at the fractional index
        std::complex<float> current_sample =
            interpolate(samples[i], samples[i + 1], frac);

        // 2. Hard QPSK decision
        std::complex<float> current_decision = qpsk_decision(current_sample);

        out.push_back(current_sample);

        // 3. Mueller-Müller timing error detector
        double error =
            static_cast<double>(
                (prev_decision_ * std::conj(current_sample) -
                 current_decision * std::conj(prev_sample_))
                    .real());

        // 4. Update loop filter
        mu_    += gain_mu_ * error;
        omega_ += gain_omega_ * error;

        // Clamp omega to prevent runaway
        double omega_min = static_cast<double>(sps_) * 0.5;
        double omega_max = static_cast<double>(sps_) * 1.5;
        if (omega_ < omega_min) omega_ = omega_min;
        if (omega_ > omega_max) omega_ = omega_max;

        // 5. Store for next iteration
        prev_sample_   = current_sample;
        prev_decision_ = current_decision;

        // Advance index by omega + mu (mu is the fractional correction)
        index += omega_ + mu_;
        mu_ = 0.0;  // mu absorbed into index advance
    }

    return out;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
double TimingRecovery::get_timing_offset() const {
    return mu_;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------
void TimingRecovery::reset() {
    mu_    = 0.0;
    omega_ = static_cast<double>(sps_);
    prev_sample_   = {0.0f, 0.0f};
    prev_decision_ = {0.0f, 0.0f};
}

} // namespace qpsk_b200
