#pragma once

/**
 * @file channel_ops.hpp
 * @brief Building blocks for simulating a radio channel: delay, carrier rotation, noise.
 *
 * Each does one thing to a block of complex baseband samples, so the channel
 * model (task 8) and the receiver tests can combine them in any order.
 */

#include <complex>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace mpsk::sim {

/**
 * Gaussian noise source with reproducible output.
 *
 * Seeded Mersenne Twister, converted to uniform numbers and then to Gaussians by
 * the Box-Muller transform written out here. The standard library's
 * std::normal_distribution is deliberately not used: the standard does not fix
 * its algorithm, so libc++ (macOS) and libstdc++ (Linux) would give different
 * numbers from the same seed, and a statistical test tuned on one could fail on
 * the other.
 */
class NoiseSource {
 public:
  explicit NoiseSource(std::uint64_t seed);

  /** One sample of N(0, 1). */
  double gaussian();

  /** Uniform in [0, 1), 53 bits of precision. */
  double uniform();

  /**
   * A complex noise sample whose total variance E[|n|^2] is @p variance, split
   * equally between the real and imaginary parts.
   */
  std::complex<float> complex_noise(double variance);

 private:
  std::mt19937_64 engine_;
  bool have_spare_ = false;
  double spare_ = 0.0;
};

/**
 * Delays a signal by a possibly fractional number of samples, using windowed-sinc
 * interpolation. The result is longer than the input by ceil(delay) plus the
 * kernel half width, so no part of the signal is cut off, and it starts with
 * zeros. Delay must be at least 0.
 *
 * Accurate for signals well inside the band, which holds for the pulse-shaped
 * signals here (see interpolator.hpp).
 *
 * @throws std::invalid_argument for a negative or non-finite delay.
 */
std::vector<std::complex<float>> delay_signal(const std::vector<std::complex<float>>& x,
                                              double delay_samples, int half_width = 8);

/**
 * Multiplies the signal by a rotating phasor: y[n] = x[n] * exp(j (2 pi f (n + start) + phase)),
 * which is a carrier frequency offset of f cycles per sample and a phase offset.
 * @param cycles_per_sample  f. A fraction of the sample rate: f = 0.01 is 1% of it.
 * @param start_index        Added to the sample index, so a block cut from a longer
 *                           signal keeps the same phase track.
 */
void rotate_carrier(std::vector<std::complex<float>>& x, double cycles_per_sample,
                    double phase_rad, double start_index = 0.0);

/** Adds complex Gaussian noise of the given total variance to every sample. */
void add_noise(std::vector<std::complex<float>>& x, double variance, NoiseSource& noise);

/**
 * Noise variance per complex sample that gives the requested symbol SNR.
 *
 * Derivation: the noise power in the sampled bandwidth Fs is N0 * Fs, so
 * variance = N0 * Fs. A symbol lasts 1/Rs, so the energy per symbol is
 * Es = Ps / Rs, where Ps is the average signal power per sample while the signal
 * is on the air. Then Es/N0 = Ps * Fs / (Rs * variance) = Ps * sps / variance, so
 *
 *     variance = Ps * sps / (Es/N0)
 *
 * With unit-energy filter taps on both sides the noise keeps this variance at the
 * matched filter output, and the symbol-spaced signal power there is Ps * sps, so
 * the symbol SNR at the receiver equals Es/N0.
 *
 * @param esn0_db  Es/N0 in dB.
 */
double noise_variance_for_esn0(double signal_power_per_sample, int samples_per_symbol,
                               double esn0_db);

/** Mean of |x|^2 over [begin, end). Returns 0 for an empty range. */
double mean_power(const std::vector<std::complex<float>>& x, std::size_t begin, std::size_t end);

}  // namespace mpsk::sim
