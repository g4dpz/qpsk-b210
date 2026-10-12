#include "sim/channel.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "sim/channel_ops.hpp"

namespace mpsk::sim {

namespace {

constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

void require_finite(double v, const char* name) {
  if (!std::isfinite(v)) {
    throw std::invalid_argument(std::string("Channel: ") + name + " must be a finite number, got " +
                                std::to_string(v));
  }
}

// A second source for the phase random walk, so that turning the walk on or off does
// not change the noise a given seed produces. The constant is the golden-ratio
// increment used by splitmix64; any odd constant would do.
constexpr std::uint64_t kWalkSeedOffset = 0x9E3779B97F4A7C15ULL;

}  // namespace

Channel::Channel(const ChannelConfig& config) : config_(config) {
  require_finite(config.sample_rate_hz, "sample_rate_hz");
  if (!(config.sample_rate_hz > 0.0)) {
    throw std::invalid_argument("Channel: sample_rate_hz must be positive, got " +
                                std::to_string(config.sample_rate_hz));
  }
  if (config.samples_per_symbol < 1) {
    throw std::invalid_argument("Channel: samples_per_symbol must be at least 1, got " +
                                std::to_string(config.samples_per_symbol));
  }
  require_finite(config.delay_samples, "delay_samples");
  if (config.delay_samples < 0.0) {
    throw std::invalid_argument("Channel: delay_samples must be at least 0, got " +
                                std::to_string(config.delay_samples));
  }
  require_finite(config.gain_db, "gain_db");
  require_finite(config.cfo_hz, "cfo_hz");
  require_finite(config.phase_rad, "phase_rad");
  require_finite(config.freq_drift_hz_per_s, "freq_drift_hz_per_s");
  require_finite(config.phase_walk_rad_per_sqrt_s, "phase_walk_rad_per_sqrt_s");
  if (config.phase_walk_rad_per_sqrt_s < 0.0) {
    throw std::invalid_argument("Channel: phase_walk_rad_per_sqrt_s must be at least 0, got " +
                                std::to_string(config.phase_walk_rad_per_sqrt_s));
  }
  require_finite(config.clock_offset_ppm, "clock_offset_ppm");
  if (!(1.0 + config.clock_offset_ppm * 1e-6 > 0.0)) {
    throw std::invalid_argument("Channel: 1 + clock_offset_ppm * 1e-6 must be positive, got ppm " +
                                std::to_string(config.clock_offset_ppm));
  }
  if (config.esn0_db) require_finite(*config.esn0_db, "esn0_db");
}

std::vector<std::complex<float>> Channel::apply(const std::vector<std::complex<float>>& x,
                                                double signal_power) const {
  const ChannelConfig& c = config_;
  if (c.esn0_db && !(signal_power > 0.0 && std::isfinite(signal_power))) {
    throw std::invalid_argument(
        "Channel::apply: noise was requested, so signal_power must be positive and finite, got " +
        std::to_string(signal_power));
  }

  // 1. Bulk delay. The whole samples are silence in front; only the fractional part
  //    needs the interpolator, which keeps a delay of many frames cheap.
  const double whole = std::floor(c.delay_samples);
  const double fraction = c.delay_samples - whole;
  std::vector<std::complex<float>> r;
  if (fraction > 0.0) {
    r = delay_signal(x, fraction);
  } else {
    r = x;
  }
  if (whole > 0.0) {
    r.insert(r.begin(), static_cast<std::size_t>(whole), std::complex<float>(0.0f, 0.0f));
  }

  // 2. Amplitude.
  const double amplitude = std::pow(10.0, c.gain_db / 20.0);
  if (amplitude != 1.0) {
    const float a = static_cast<float>(amplitude);
    for (auto& v : r) v *= a;
  }

  // 3 and 4. One phase track: starting phase, frequency offset, frequency ramp, and
  //    random walk. Computed in double from the sample index, not accumulated, so a
  //    long record does not pick up rounding error in the carrier.
  const bool rotate = c.cfo_hz != 0.0 || c.phase_rad != 0.0 || c.freq_drift_hz_per_s != 0.0 ||
                      c.phase_walk_rad_per_sqrt_s != 0.0;
  if (rotate) {
    NoiseSource walk_rng(c.seed ^ kWalkSeedOffset);
    const double dt = 1.0 / c.sample_rate_hz;
    const double walk_step = c.phase_walk_rad_per_sqrt_s * std::sqrt(dt);
    double walk = 0.0;
    for (std::size_t n = 0; n < r.size(); ++n) {
      const double t = static_cast<double>(n) * dt;
      if (walk_step != 0.0) walk += walk_step * walk_rng.gaussian();
      const double cycles = c.cfo_hz * t + 0.5 * c.freq_drift_hz_per_s * t * t;
      const double angle = kTwoPi * cycles + c.phase_rad + walk;
      const std::complex<double> v(static_cast<double>(r[n].real()), static_cast<double>(r[n].imag()));
      const std::complex<double> rotated = v * std::polar(1.0, angle);
      r[n] = {static_cast<float>(rotated.real()), static_cast<float>(rotated.imag())};
    }
  }

  // 5. Sample-clock offset.
  if (c.clock_offset_ppm != 0.0) r = apply_clock_offset(r, c.clock_offset_ppm);

  // 6. Noise, against the signal as it now is.
  if (c.esn0_db) {
    NoiseSource noise(c.seed);
    const double received_power = signal_power * amplitude * amplitude;
    add_noise(r, noise_variance_for_esn0(received_power, c.samples_per_symbol, *c.esn0_db), noise);
  }
  return r;
}

}  // namespace mpsk::sim
