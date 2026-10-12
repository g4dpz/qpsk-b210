#pragma once

/**
 * @file preamble_detector.hpp
 * @brief Finding the preamble in the matched-filter output, and measuring it.
 *
 * Input is the output of RxFrontEnd: the signal after DC removal and the matched
 * RRC filter, at the sample rate. A frame's preamble is 64 BPSK symbols. In the
 * input, symbol i of the preamble has its pulse peak at index
 *
 *     position + i * sps        (position may be fractional)
 *
 * and, for a signal with carrier frequency offset w (radians per symbol), carrier
 * phase and amplitude A, the symbol-spaced samples there are
 *
 *     y_i = A * exp(j (phi_mid + w * (i - 31.5))) * s_i   +  noise
 *
 * where s_i = +1 or -1 is the preamble. Phase is referenced to the centre of the
 * preamble (i = 31.5), not its first symbol, for a reason given below.
 *
 * Two stages, because no single threshold does both jobs.
 *
 * Stage 1, coarse detection. The 64 preamble symbols are split into four
 * segments of 16. For a candidate position t the segment correlations are
 *
 *     c_k = sum over i in segment k of  y(t + i*sps) * s_i          (k = 0..3)
 *
 * and the statistic is  D = sum_k |c_k|^2 / (16 * E),  with E the energy of the 64
 * samples used. By the Cauchy-Schwarz inequality D is between 0 and 1, and it is 1
 * only when every segment of y matches the preamble exactly. D ignores the phase
 * of each c_k, so a carrier offset (which rotates one segment relative to the
 * next) does not matter, and it is divided by the energy, so the received level
 * does not matter either. Both properties are what the receiver needs.
 *
 * For noise alone, the 64 samples are independent (a Nyquist pulse has no
 * correlation at symbol spacing), and D is the squared length of the projection
 * of a random unit vector in C^64 onto a fixed 4-dimensional subspace, which is
 * Beta(4, 60) distributed: mean 1/16, standard deviation 0.030, and
 *
 *     P(D > t) = sum_{j=0}^{3} C(63, j) t^j (1 - t)^(63 - j).
 *
 * The cost of ignoring carrier offset within a segment: across 16 symbols a
 * frequency offset f rotates the phase by 2 pi f * 16 / Rs, and the coherent sum
 * of a segment shrinks by sinc(16 f / Rs), so the peak of D is sinc^2(16 f / Rs):
 * 0.81 at a quarter of the acquisition limit, 0.405 at the limit itself. A
 * threshold that high enough to give few false triggers on noise (P = 2e-5 per
 * position at 0.25, so about 23 per second at 1 MS/s, and far fewer only above
 * about 0.5) would miss a signal near the edge of the range. Hence stage 2.
 *
 * Stage 2, estimation and verification. At the stage-1 peak:
 *   - frequency: the Luise-Reggiannini estimate from the phase advance between
 *     adjacent segments, w = arg( sum_k c_k conj(c_{k-1}) ) / 16. Adjacent
 *     segments are 16 symbols apart, so a phase advance is unambiguous only below
 *     pi, i.e. |w| < pi / 16 rad/symbol, or |f| < Rs / 32 (7.8 kHz at 250 ksym/s).
 *   - timing: the coherent correlation magnitude, with the frequency removed, is
 *     evaluated at the integer positions around the peak. A parabola through the
 *     best three gives the fractional position.
 *   - phase and amplitude: with the timing refined (by windowed-sinc
 *     interpolation), the frequency is estimated again and the coherent sum
 *     C = sum_i y_i s_i exp(-j w (i - 31.5)) gives phase = arg(C) and amplitude
 *     A = |C| / 64.
 *   - verification: V = |C|^2 / (64 E) is between 0 and 1. For a real preamble at
 *     symbol SNR g it is about g / (1 + g). For noise it is about 1/64 on average
 *     and has a thin tail, so a modest threshold separates the two cleanly.
 *
 * Why the phase is referenced to the centre. The coherent sum is taken after
 * removing the estimated frequency w_hat. If the estimate is off by e, the phase
 * of C is the true phase plus e times the centroid of the weights, and the
 * centroid of (i - 31.5) is zero. So the centre phase is unaffected by the
 * frequency error, while a phase referenced to symbol 0 would carry 31.5 * e.
 * The receiver extrapolates from the centre with phase_at().
 */

#include <complex>
#include <cstddef>
#include <optional>

#include "dsp/preamble.hpp"

namespace mpsk::dsp {

/** Preamble segments used by the detector and the frequency estimator. */
constexpr int kPreambleSegments = kPreambleSymbols / kPreambleSegmentSymbols;

/**
 * Largest carrier frequency offset the estimator can resolve, in cycles per
 * symbol: 1 / (2 * 16). Multiply by the symbol rate for Hz.
 */
constexpr double kMaxCfoCyclesPerSymbol = 1.0 / (2.0 * kPreambleSegmentSymbols);

/** Index of the centre of the preamble, the phase reference: (64 - 1) / 2. */
constexpr double kPreambleCentre = (kPreambleSymbols - 1) / 2.0;

struct AcquisitionConfig {
  int samples_per_symbol = 4;

  /**
   * Stage-1 threshold on D. For noise alone P(D > t) follows the Beta(4, 60) law
   * above: 2.3e-5 per position at 0.25. Low enough that a signal at the edge of
   * the frequency range (peak D of 0.405 at high SNR) is still caught.
   */
  double coarse_threshold = 0.25;

  /**
   * Stage-2 threshold on V. A preamble at symbol SNR g gives V of about
   * g / (1 + g), so 0.30 detects down to g = 0.43 (-3.7 dB). Noise rarely
   * exceeds 0.1; see the tests for measured figures.
   */
  double verify_threshold = 0.30;
};

/** The measurements taken from a preamble. */
struct PreambleEstimate {
  /** Index in the input of the pulse peak of the first preamble symbol. May be fractional. */
  double position = 0.0;

  /** Carrier frequency offset in radians per symbol, within +-pi/16. */
  double omega = 0.0;

  /** Carrier phase at the centre of the preamble (symbol index 31.5), radians. */
  double phase_centre = 0.0;

  /**
   * Received amplitude of a unit-magnitude symbol, in the units of the input.
   * Equals the transmit scale times the channel gain.
   */
  double amplitude = 0.0;

  /** Stage-1 statistic D at the coarse peak. */
  double coarse_metric = 0.0;

  /** Stage-2 statistic V. */
  double verify_metric = 0.0;

  /** Carrier phase extrapolated to symbol @p symbol_index (counted from the first preamble symbol). */
  double phase_at(double symbol_index) const {
    return phase_centre + omega * (symbol_index - kPreambleCentre);
  }

  /** Frequency offset in Hz, given the symbol rate in Hz. */
  double cfo_hz(double symbol_rate_hz) const { return omega * symbol_rate_hz / (2.0 * 3.14159265358979323846); }
};

/** Counters from a scan, for diagnostics and for measuring false-alarm behaviour. */
struct AcquisitionStats {
  std::size_t positions_scanned = 0;   ///< stage-1 evaluations in the main scan
  std::size_t candidates = 0;          ///< stage-1 threshold crossings handed to stage 2
  std::size_t rejected = 0;            ///< candidates stage 2 refused
  double max_rejected_verify = 0.0;    ///< largest V among the rejected, to show the margin
};

/** Samples spanned by the 64 preamble symbols at @p sps samples per symbol: 63 * sps + 1. */
std::size_t preamble_span_samples(int sps);

/**
 * Samples needed on each side of the preamble for stage 2: the timing search and
 * the interpolation kernel read this far beyond the span. A candidate position t
 * is usable only if t >= margin and t + span + margin <= n.
 */
std::size_t acquisition_margin(int sps);

/**
 * Stage-1 statistic D at integer position @p t. Requires t + preamble_span <= n.
 * Returns 0 if the 64 samples have no energy (digital silence).
 */
double coarse_metric(const std::complex<float>* y, std::size_t n, std::size_t t, int sps);

/**
 * Stage 2 at an approximate position: measures frequency, fractional timing,
 * phase and amplitude, and the verification statistic V. Does not apply the
 * verification threshold; the caller decides.
 *
 * @param t_coarse  Integer position of the stage-1 peak, ideally within one sample
 *                  of the true position (it is searched +-sps/2).
 * @return std::nullopt if there are not enough samples around @p t_coarse (see
 *         acquisition_margin()).
 * @throws std::invalid_argument for an unusable configuration.
 */
std::optional<PreambleEstimate> estimate_preamble(const std::complex<float>* y, std::size_t n,
                                                  std::size_t t_coarse,
                                                  const AcquisitionConfig& config);

/**
 * Scans @p y from index @p start for the first preamble that passes both stages.
 *
 * At each position the stage-1 statistic is computed. At a crossing of the coarse
 * threshold the maximum over the next sps positions is taken as the peak, stage 2
 * measures and verifies it, and on success the estimate is returned. A refused
 * candidate is skipped and the scan continues past it, so a false trigger before a
 * real preamble does not hide it.
 *
 * The returned position is when the preamble has been fully seen, and is valid
 * only if the buffer extends at least acquisition_margin() past the preamble's
 * span.
 *
 * @param stats  If not null, receives the counters.
 * @return std::nullopt if no preamble was found in the buffer.
 * @throws std::invalid_argument for an unusable configuration.
 */
std::optional<PreambleEstimate> acquire_preamble(const std::complex<float>* y, std::size_t n,
                                                 std::size_t start,
                                                 const AcquisitionConfig& config,
                                                 AcquisitionStats* stats = nullptr);

}  // namespace mpsk::dsp
