#pragma once

/**
 * @file metrics.hpp
 * @brief Link-quality measurements: EVM, bit errors, signal level and clipping.
 *
 * Requirements 4.1, 4.2 (per-frame statistics, BER against a known payload) and
 * 7.11, 7.12 (report the level; warn when the input is clipping).
 */

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mpsk::dsp {

// ---- Error vector magnitude -------------------------------------------------

/**
 * Accumulates the error vector magnitude of corrected symbols against their
 * decisions.
 *
 * EVM is the RMS distance from each received symbol to the point it was decided
 * to be, relative to the RMS magnitude of the constellation (1, because the points
 * have unit average power):
 *
 *     EVM = sqrt( mean |y - d|^2 )
 *
 * Where noise is the only impairment, mean |y - d|^2 is the noise power at the
 * decision instant, so 1 / EVM^2 is the symbol SNR, and snr_db() = -20 log10(EVM).
 *
 * That holds while decisions are mostly right. A wrong decision measures the
 * distance to the wrong point, which is smaller than the true error, so at low SNR
 * the figure is optimistic: it reads high. Use it as a link-quality indicator, not
 * as a measurement below about 6 dB.
 *
 * It also includes everything else that moves a symbol off its point, such as
 * residual inter-symbol interference and carrier phase error, so on a clean signal
 * it reads a small non-zero floor rather than zero.
 */
class EvmAccumulator {
 public:
  /** Adds one symbol: the corrected, unit-amplitude sample and the point decided for it. */
  void add(std::complex<float> corrected, std::complex<float> decision);

  std::size_t count() const { return count_; }

  /** RMS EVM as a fraction (not percent). 0 if no symbols were added. */
  double rms() const;

  /** Estimated symbol SNR in dB, -20 log10(EVM). +infinity if the EVM is exactly 0. */
  double snr_db() const;

 private:
  double sum_squares_ = 0.0;
  std::size_t count_ = 0;
};

/** -20 log10(evm): the symbol SNR a given EVM implies. +infinity for 0. */
double evm_to_snr_db(double evm);

// ---- Bit errors -------------------------------------------------------------

struct BerResult {
  std::size_t bit_errors = 0;
  std::size_t bits_compared = 0;

  /**
   * True if the decoded and expected payloads had different lengths. The
   * comparison then covers only the shorter, and the bits of the longer that have
   * no counterpart are not counted either way. A length mismatch means a frame was
   * not what was expected, so the caller should treat it as a fault, not trust the
   * rate.
   */
  bool length_mismatch = false;

  /** Bit error rate, errors / bits compared. 0 if nothing was compared. */
  double rate() const {
    return bits_compared == 0 ? 0.0 : static_cast<double>(bit_errors) / static_cast<double>(bits_compared);
  }
};

/** Counts the bits that differ between a decoded payload and the expected one. */
BerResult payload_ber(const std::vector<std::uint8_t>& decoded,
                      const std::vector<std::uint8_t>& expected);

// ---- Signal level and clipping ---------------------------------------------

struct SignalLevel {
  /**
   * Mean power of the examined samples in dB relative to full scale, where a complex
   * sample of magnitude 1 counts as 0 dBFS: 10 log10(mean |x|^2). A full-scale
   * square in both I and Q reads +3 dBFS. This is not calibrated dBm: it says how
   * the signal sits against the converter's range, not how strong it is at the
   * antenna. -200 for an empty or silent range.
   */
  double rms_dbfs = -200.0;

  double peak_magnitude = 0.0;        ///< largest |x|
  std::size_t clipped_samples = 0;    ///< samples with |I| or |Q| at or above the threshold
  std::size_t samples = 0;            ///< samples examined
};

/** Default clipping threshold, as a fraction of full scale. */
constexpr double kDefaultClipThreshold = 0.98;

/**
 * Measures the level and counts clipped samples in raw[begin, end), meaning the
 * samples as they came from the radio, before the DC blocker (which would shift
 * them). The range is clamped to the buffer.
 *
 * A sample counts as clipped when |I| or |Q| reaches the threshold. The converter
 * saturates each component separately, so a component at the limit is clipping even
 * when the magnitude is lower.
 */
SignalLevel measure_signal_level(const std::complex<float>* raw, std::size_t n, std::size_t begin,
                                 std::size_t end, double clip_threshold = kDefaultClipThreshold);

}  // namespace mpsk::dsp
