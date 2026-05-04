#include "qpsk_b200/carrier_sync.h"

#include <cmath>
#include <stdexcept>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// Helper: sign function returning +1 or −1 (zero maps to +1)
// ---------------------------------------------------------------------------
static inline float sign(float x) {
    return x >= 0.0f ? 1.0f : -1.0f;
}

// ---------------------------------------------------------------------------
// Constructor — compute loop filter gains from normalised loop bandwidth
//
// Second-order loop gain conversion (critically damped):
//   damping = 1/√2
//   θ       = loop_bw / (damping + 1/(4·damping))
//   d       = 1 + 2·damping·θ + θ²
//   α       = (4·damping·θ) / d          (proportional)
//   β       = (4·θ²) / d                 (integral)
// ---------------------------------------------------------------------------
CarrierSynchronizer::CarrierSynchronizer(double loop_bw)
    : phase_(0.0), freq_(0.0), alpha_(0.0), beta_(0.0) {
    if (loop_bw <= 0.0 || loop_bw >= 1.0) {
        throw std::invalid_argument(
            "CarrierSynchronizer: loop_bw must be in (0, 1), got " +
            std::to_string(loop_bw));
    }

    const double damping = 1.0 / std::sqrt(2.0);
    const double theta   = loop_bw / (damping + 1.0 / (4.0 * damping));
    const double d       = 1.0 + 2.0 * damping * theta + theta * theta;

    alpha_ = (4.0 * damping * theta) / d;
    beta_  = (4.0 * theta * theta) / d;
}

// ---------------------------------------------------------------------------
// synchronize — apply Costas loop to a block of samples
// ---------------------------------------------------------------------------
std::vector<std::complex<float>>
CarrierSynchronizer::synchronize(
    const std::vector<std::complex<float>>& samples) {
    std::vector<std::complex<float>> out;
    out.reserve(samples.size());

    for (const auto& sample : samples) {
        // 1. Rotate sample by current phase estimate
        auto rotator = std::complex<float>(
            static_cast<float>(std::cos(-phase_)),
            static_cast<float>(std::sin(-phase_)));
        std::complex<float> corrected = sample * rotator;

        out.push_back(corrected);

        // 2. QPSK decision-directed phase error detector
        float I = corrected.real();
        float Q = corrected.imag();
        double error = static_cast<double>(sign(Q) * I - sign(I) * Q);

        // 3. Update loop filter (negative feedback)
        freq_  -= beta_ * error;
        phase_ -= alpha_ * error;
        phase_ += freq_;

        // 4. Wrap phase to [−π, π]
        while (phase_ > M_PI)  phase_ -= 2.0 * M_PI;
        while (phase_ < -M_PI) phase_ += 2.0 * M_PI;
    }

    return out;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
double CarrierSynchronizer::get_freq_offset() const {
    return freq_;
}

double CarrierSynchronizer::get_phase_offset() const {
    return phase_;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------
void CarrierSynchronizer::reset() {
    phase_ = 0.0;
    freq_  = 0.0;
}

} // namespace qpsk_b200
