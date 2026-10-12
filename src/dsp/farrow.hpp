#pragma once

/**
 * @file farrow.hpp
 * @brief Cubic Farrow interpolation, for the per-symbol timing loop.
 *
 * The timing loop reads the matched-filter output at a position that moves from
 * symbol to symbol and is almost never on the sample grid. That happens twice per
 * symbol, so the interpolator has to be cheap. The windowed-sinc interpolator in
 * interpolator.hpp is more accurate but costs 16 multiply-adds and a fresh set of
 * weights per read. This one costs a handful of multiply-adds.
 *
 * Method: Lagrange cubic interpolation through the four samples around the
 * position, written in Farrow form. For a position mu in [0, 1) between samples
 * x[0] and x[1], with x[-1] and x[2] as the outer neighbours,
 *
 *     y(mu) = ((v3 * mu + v2) * mu + v1) * mu + v0
 *
 *     v3 = -1/6 x[-1] + 1/2 x[0] - 1/2 x[1] + 1/6 x[2]
 *     v2 =  1/2 x[-1] -     x[0] + 1/2 x[1]
 *     v1 = -1/3 x[-1] - 1/2 x[0] +     x[1] - 1/6 x[2]
 *     v0 =                  x[0]
 *
 * These are the coefficients of the cubic through (-1, x[-1]), (0, x[0]),
 * (1, x[1]) and (2, x[2]). Check: at mu = 0 the result is x[0], and at mu = 1 the
 * x[-1], x[0] and x[2] terms cancel and x[1] is left.
 *
 * Accuracy depends on how much of the sampling rate the signal occupies. See the
 * measured error in the design and in test_farrow.
 */

#include <complex>
#include <cstddef>

namespace mpsk::dsp {

/** Samples needed before and after the interpolation point: one before, two after. */
constexpr std::size_t kFarrowBefore = 1;
constexpr std::size_t kFarrowAfter = 2;

/**
 * True if @p position can be interpolated from a signal of @p n samples, that is,
 * floor(position) - 1 >= 0 and floor(position) + 2 <= n - 1.
 */
bool farrow_in_range(std::size_t n, double position);

/**
 * Value of the signal at fractional index @p position.
 * @throws std::out_of_range if farrow_in_range() is false for this position.
 */
std::complex<float> farrow_cubic(const std::complex<float>* y, std::size_t n, double position);

}  // namespace mpsk::dsp
