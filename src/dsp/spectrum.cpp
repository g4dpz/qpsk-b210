#include "dsp/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace mpsk::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool is_power_of_two(std::size_t n) { return n != 0 && (n & (n - 1)) == 0; }

void validate(const SpectrumConfig& c) {
  if (!is_power_of_two(c.fft_size) || c.fft_size < 64) {
    throw std::invalid_argument("spectrum: fft_size must be a power of two of at least 64, got " +
                                std::to_string(c.fft_size));
  }
  if (c.smoothing_bins == 0 || c.smoothing_bins % 2 == 0) {
    throw std::invalid_argument("spectrum: smoothing_bins must be odd, got " + std::to_string(c.smoothing_bins));
  }
}

}  // namespace

void fft(std::vector<std::complex<double>>& a, bool inverse) {
  const std::size_t n = a.size();
  if (n > 1 && !is_power_of_two(n)) {
    throw std::invalid_argument("fft: size must be a power of two, got " + std::to_string(n));
  }
  if (n < 2) return;

  // Bit-reversal permutation.
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  // Butterflies. The twiddle for each stage is stepped by a rotation, which accumulates
  // a little rounding error over long stages; recomputing it from the angle every 16
  // butterflies keeps that bounded.
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double angle = (inverse ? 2.0 : -2.0) * kPi / static_cast<double>(len);
    const std::complex<double> step(std::cos(angle), std::sin(angle));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (std::size_t j = 0; j < len / 2; ++j) {
        if (j % 16 == 0) w = std::polar(1.0, angle * static_cast<double>(j));
        const std::complex<double> u = a[i + j];
        const std::complex<double> v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= step;
      }
    }
  }
  if (inverse) {
    for (auto& v : a) v /= static_cast<double>(n);
  }
}

std::vector<double> welch_psd(const std::vector<std::complex<float>>& x, const SpectrumConfig& config,
                              std::size_t* segments_used) {
  validate(config);
  const std::size_t n = config.fft_size;
  if (x.size() < n) {
    throw std::invalid_argument("spectrum: the record has " + std::to_string(x.size()) +
                                " samples, fewer than the FFT size " + std::to_string(n));
  }
  // Hann window. Periodic form (divided by N, not N - 1), the usual choice for spectral estimation.
  std::vector<double> window(n);
  for (std::size_t i = 0; i < n; ++i) {
    window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
  }

  std::vector<double> psd(n, 0.0);
  std::vector<std::complex<double>> seg(n);
  std::size_t count = 0;
  const std::size_t hop = n / 2;
  for (std::size_t start = 0; start + n <= x.size(); start += hop) {
    for (std::size_t i = 0; i < n; ++i) {
      seg[i] = std::complex<double>(static_cast<double>(x[start + i].real()), static_cast<double>(x[start + i].imag())) *
               window[i];
    }
    fft(seg);
    for (std::size_t k = 0; k < n; ++k) psd[k] += std::norm(seg[k]);
    ++count;
  }
  for (double& p : psd) p /= static_cast<double>(count);
  if (segments_used) *segments_used = count;
  return psd;
}

OccupiedBandwidth measure_occupied_bandwidth(const std::vector<std::complex<float>>& x, double sample_rate_hz,
                                             double symbol_rate_hz, const SpectrumConfig& config) {
  if (!(sample_rate_hz > 0.0) || !std::isfinite(sample_rate_hz)) {
    throw std::invalid_argument("spectrum: the sample rate must be positive and finite, got " +
                                std::to_string(sample_rate_hz));
  }
  if (!(symbol_rate_hz > 0.0) || !std::isfinite(symbol_rate_hz)) {
    throw std::invalid_argument("spectrum: the symbol rate must be positive and finite, got " +
                                std::to_string(symbol_rate_hz));
  }
  OccupiedBandwidth out;
  const std::vector<double> psd = welch_psd(x, config, &out.segments);
  const std::size_t n = psd.size();
  const double df = sample_rate_hz / static_cast<double>(n);
  out.resolution_hz = df;

  // |f|-ordered view: ring m holds the bins at +m df and -m df.
  const std::size_t half = n / 2;
  std::vector<double> ring(half + 1, 0.0);
  ring[0] = psd[0];
  for (std::size_t m = 1; m < half; ++m) ring[m] = psd[m] + psd[n - m];
  ring[half] = psd[half];

  double total = 0.0;
  for (double r : ring) total += r;

  auto power_width = [&](double fraction) {
    double acc = 0.0;
    for (std::size_t m = 0; m <= half; ++m) {
      acc += ring[m];
      if (acc >= fraction * total) return 2.0 * static_cast<double>(m) * df;
    }
    return 2.0 * static_cast<double>(half) * df;
  };
  out.power_99_hz = power_width(0.99);
  out.power_999_hz = power_width(0.999);

  // In-band reference: mean PSD within 0.3 symbol rates of the centre.
  const double reference_edge = 0.3 * symbol_rate_hz;
  double ref_sum = 0.0;
  std::size_t ref_bins = 0;   // at least the bin at 0 Hz, which is always inside
  for (std::size_t k = 0; k < n; ++k) {
    const double f = (k <= half ? static_cast<double>(k) : static_cast<double>(k) - static_cast<double>(n)) * df;
    if (std::fabs(f) < reference_edge) {
      ref_sum += psd[k];
      ++ref_bins;
    }
  }
  const double reference = ref_sum / static_cast<double>(ref_bins);

  // Smoothed PSD, wrapping around (it is a ring in frequency).
  const std::size_t reach = config.smoothing_bins / 2;
  std::vector<double> smooth(n, 0.0);
  for (std::size_t k = 0; k < n; ++k) {
    double acc = 0.0;
    for (std::size_t d = 0; d <= 2 * reach; ++d) acc += psd[(k + n + d - reach) % n];
    smooth[k] = acc / static_cast<double>(config.smoothing_bins);
  }
  auto skirt_width = [&](double db) {
    const double threshold = reference * std::pow(10.0, -db / 10.0);
    std::size_t widest = 0;
    for (std::size_t k = 0; k < n; ++k) {
      if (smooth[k] > threshold) widest = std::max(widest, k <= half ? k : n - k);
    }
    return 2.0 * static_cast<double>(widest) * df;
  };
  out.width_26_db_hz = skirt_width(26.0);
  out.width_40_db_hz = skirt_width(40.0);
  return out;
}

}  // namespace mpsk::dsp
