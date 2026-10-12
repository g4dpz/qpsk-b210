#pragma once

/**
 * @file interpolator.hpp
 * @brief Windowed-sinc interpolation of a band-limited signal at a fractional index.
 *
 * Used where the position is not on the sample grid and accuracy matters more
 * than speed: reading the preamble at its estimated fractional timing, and
 * delaying a simulated signal by a fraction of a sample. (The per-sample timing
 * loop uses a cheaper cubic interpolator instead.)
 *
 * The ideal interpolator for a signal band-limited to below half the sampling
 * rate is a sinc kernel, which is infinitely long. Cutting it to 2H taps and
 * multiplying by a Blackman window keeps the error small for signals well inside
 * the band. The signals here are root-raised-cosine shaped and occupy only
 * (1 + roll-off) / 2 / sps of the sampling rate, about 17% at 4 samples per
 * symbol, so a short kernel is accurate.
 */

#include <complex>
#include <cstddef>
#include <vector>

namespace mpsk::dsp {

/** Default half-width H of the kernel: 2H = 16 taps. */
constexpr int kDefaultInterpolationHalfWidth = 8;

/**
 * Kernel weights for interpolating at a position @p frac of the way from sample
 * i0 to sample i0 + 1 (0 <= frac < 1).
 *
 * The result has 2H weights; weight [j] applies to sample i0 + (j - H + 1), that
 * is, samples i0 - H + 1 ... i0 + H. They are normalised to sum to exactly 1, so a
 * constant signal is interpolated without error. A frac of 0 gives a single 1 at
 * the sample itself, so interpolating on the grid returns the sample unchanged.
 *
 * @throws std::invalid_argument for half_width < 1 or frac outside [0, 1).
 */
std::vector<double> sinc_interpolation_weights(double frac, int half_width);

/**
 * Value of the signal at fractional index @p position, from the 2H nearest
 * samples. Samples outside [0, n) count as zero.
 */
std::complex<float> sinc_interpolate(const std::complex<float>* y, std::size_t n, double position,
                                     int half_width = kDefaultInterpolationHalfWidth);

}  // namespace mpsk::dsp
