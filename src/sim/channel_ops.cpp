#include "sim/channel_ops.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "dsp/interpolator.hpp"

namespace mpsk::sim {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
}  // namespace

NoiseSource::NoiseSource(std::uint64_t seed) : engine_(seed) {}

double NoiseSource::uniform() {
  // The top 53 bits of a 64-bit draw, scaled to [0, 1). Every such value is
  // exactly representable, and the result never reaches 1.
  return static_cast<double>(engine_() >> 11) * (1.0 / 9007199254740992.0);
}

double NoiseSource::gaussian() {
  // Box-Muller produces two independent Gaussians from two uniforms. Return one
  // now and keep the other for the next call.
  if (have_spare_) {
    have_spare_ = false;
    return spare_;
  }
  double u1;
  do {
    u1 = uniform();
  } while (u1 <= 0.0);  // log(0) is undefined; this happens once in 2^53 draws
  const double u2 = uniform();
  const double radius = std::sqrt(-2.0 * std::log(u1));
  spare_ = radius * std::sin(kTwoPi * u2);
  have_spare_ = true;
  return radius * std::cos(kTwoPi * u2);
}

std::complex<float> NoiseSource::complex_noise(double variance) {
  const double sigma = std::sqrt(variance / 2.0);  // per component
  const double re = sigma * gaussian();
  const double im = sigma * gaussian();
  return {static_cast<float>(re), static_cast<float>(im)};
}

std::vector<std::complex<float>> delay_signal(const std::vector<std::complex<float>>& x,
                                              double delay_samples, int half_width) {
  if (!(delay_samples >= 0.0) || !std::isfinite(delay_samples)) {
    throw std::invalid_argument("delay_signal: delay must be a finite number >= 0, got " +
                                std::to_string(delay_samples));
  }
  const std::size_t extra = static_cast<std::size_t>(std::ceil(delay_samples)) +
                            static_cast<std::size_t>(half_width);
  std::vector<std::complex<float>> out(x.size() + extra, std::complex<float>(0.0f, 0.0f));

  // out[m] = x(m - delay). Every output sample reads at the same fractional offset,
  // so one set of interpolation weights serves them all.
  const double first_position = -delay_samples;            // read position for m = 0
  const double base0 = std::floor(first_position);
  const std::vector<double> w =
      mpsk::dsp::sinc_interpolation_weights(first_position - base0, half_width);
  const long shift = static_cast<long>(base0);

  for (std::size_t m = 0; m < out.size(); ++m) {
    double re = 0.0;
    double im = 0.0;
    for (int k = -half_width + 1; k <= half_width; ++k) {
      const long idx = static_cast<long>(m) + shift + k;
      if (idx < 0 || static_cast<std::size_t>(idx) >= x.size()) continue;
      const double weight = w[static_cast<std::size_t>(k + half_width - 1)];
      re += weight * static_cast<double>(x[static_cast<std::size_t>(idx)].real());
      im += weight * static_cast<double>(x[static_cast<std::size_t>(idx)].imag());
    }
    out[m] = {static_cast<float>(re), static_cast<float>(im)};
  }
  return out;
}

void rotate_carrier(std::vector<std::complex<float>>& x, double cycles_per_sample,
                    double phase_rad, double start_index) {
  for (std::size_t n = 0; n < x.size(); ++n) {
    const double angle = kTwoPi * cycles_per_sample * (static_cast<double>(n) + start_index) +
                         phase_rad;
    const std::complex<double> v(static_cast<double>(x[n].real()), static_cast<double>(x[n].imag()));
    const std::complex<double> r = v * std::polar(1.0, angle);
    x[n] = {static_cast<float>(r.real()), static_cast<float>(r.imag())};
  }
}

void add_noise(std::vector<std::complex<float>>& x, double variance, NoiseSource& noise) {
  for (auto& s : x) s += noise.complex_noise(variance);
}

double noise_variance_for_esn0(double signal_power_per_sample, int samples_per_symbol,
                               double esn0_db) {
  const double esn0 = std::pow(10.0, esn0_db / 10.0);
  return signal_power_per_sample * static_cast<double>(samples_per_symbol) / esn0;
}

double mean_power(const std::vector<std::complex<float>>& x, std::size_t begin, std::size_t end) {
  if (end > x.size()) end = x.size();
  if (begin >= end) return 0.0;
  double sum = 0.0;
  for (std::size_t i = begin; i < end; ++i) sum += static_cast<double>(std::norm(x[i]));
  return sum / static_cast<double>(end - begin);
}

}  // namespace mpsk::sim
