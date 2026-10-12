#pragma once

/**
 * @file dc_blocker.hpp
 * @brief One-pole DC blocker for the receive chain.
 *
 *   y[n] = x[n] - x[n-1] + a * y[n-1]
 *
 * The zero at DC cancels a constant offset, and the pole at a (just inside the
 * unit circle) restores the gain a short distance away from DC. With a = 0.9999
 * at 1 MS/s the -3 dB corner is about (1 - a) * Fs / (2 pi) = 16 Hz, so the
 * blocker removes LO leakage and converter offset without touching a signal
 * whose bandwidth is hundreds of kilohertz.
 *
 * Start-up needs care, and the obvious fixes each cost something.
 *
 * Starting from a zero state is too slow. The filter then passes a DC offset and
 * lets it decay with a time constant of 1 / (1 - a) = 10 000 samples (10 ms), so
 * 76% of the offset is still there 2.7 ms later, the length of a whole frame, and
 * the frame rides on a decaying step that corrupts the amplitude and phase
 * estimates.
 *
 * Priming from a single sample is too noisy. The filter then starts as if the
 * input had always been that one sample, so the sample's own noise is mistaken for
 * DC and also decays over 10 ms. Measured on white noise, the noise power gain is
 * 1.89 just after such a start, 1.6 at 2.7 ms, and about 20 ms pass before it
 * reaches 1. That costs roughly 2 dB for a frame that arrives soon after a reset.
 *
 * So process() primes from the mean of the first prime_samples samples of the
 * stretch, then filters the whole block from that state. The error of a mean falls
 * as 1/N. Measured with N = 256: the noise power gain in the first 2.7 ms is 1.002
 * and the residual DC is about 0.5% of the offset. At N = 1 the same figures are
 * 1.70 and 8%. The cost is that the block must be at least that long to benefit;
 * a shorter first block is primed from its own mean, the best it can do.
 *
 * push() works one sample at a time and cannot look ahead, so it can only prime
 * from the sample it is given, which is the noisy N = 1 case. Use process().
 */

#include <complex>
#include <cstddef>

namespace mpsk::dsp {

/** Default number of samples averaged to find the DC level after a reset. */
constexpr std::size_t kDefaultDcPrimeSamples = 256;

class DcBlocker {
 public:
  /**
   * @param pole           The pole a, in [0, 1). Closer to 1 gives a lower corner
   *                       frequency and a slower start-up.
   * @param prime_samples  How many samples at the start of a stretch are averaged
   *                       to find the DC level, at least 1.
   * @throws std::invalid_argument for a pole outside [0, 1) or prime_samples of 0.
   */
  explicit DcBlocker(double pole = 0.9999, std::size_t prime_samples = kDefaultDcPrimeSamples);

  /**
   * Filters one sample. If the filter has just been reset, it primes from this
   * sample alone (see the note above on why that is noisy).
   */
  std::complex<float> push(std::complex<float> x);

  /**
   * Filters @p n samples. The ranges may be the same (in-place). If the filter
   * has just been reset, it first primes from the mean of the first
   * min(n, prime_samples) samples of this block.
   */
  void process(const std::complex<float>* in, std::complex<float>* out, std::size_t n);

  /**
   * Forgets the DC estimate. The next samples prime the filter again. Call this
   * after the unit's own transmission, when the receiver has been disturbed.
   */
  void reset();

  double pole() const { return pole_; }
  std::size_t prime_samples() const { return prime_samples_; }

 private:
  /** Starts the filter as if the input had always been @p dc: previous input dc, output 0. */
  void prime_from(std::complex<double> dc);

  double pole_;
  std::size_t prime_samples_;
  std::complex<double> x_prev_{0.0, 0.0};
  std::complex<double> y_prev_{0.0, 0.0};
  bool primed_ = false;
};

}  // namespace mpsk::dsp
