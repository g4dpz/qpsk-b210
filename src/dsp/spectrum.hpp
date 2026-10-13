#pragma once

/**
 * @file spectrum.hpp
 * @brief Power spectrum and occupied bandwidth of a baseband signal.
 *
 * Needed for the narrowband profile (Requirement 12): the QO-100 narrowband
 * transponder allows a signal at most 2.7 kHz wide, and this measures what the
 * modulator actually produces rather than trusting `Rs (1 + roll-off)`. The
 * formula is exact for an ideal, infinitely long root-raised-cosine pulse; a real
 * one is truncated to a few symbols, and that truncation puts energy outside it.
 *
 * The estimate is Welch's method: Hann-windowed segments, 50% overlap, averaged
 * periodograms. Frequencies are measured from the centre of the record (0 Hz), so
 * the signal must be centred there, as the modulator's output is.
 *
 * Definitions, all symmetric about 0 Hz:
 *   - the 99% (or 99.9%) power width is the narrowest band that holds that
 *     fraction of the total power;
 *   - the width at -X dB is twice the largest frequency at which the PSD, smoothed
 *     over a few bins, is still above the in-band level minus X dB. The in-band
 *     level is the mean PSD within 0.3 symbol rates of the centre. This is "how far
 *     out do the skirts reach", and it is the figure to hold against a spectral mask.
 *     A noisy signal whose noise floor is above the threshold reports the full
 *     sample rate, because the floor is out there. The smoothing makes the skirt
 *     widths read a little wide, by up to half the smoothing length on each side
 *     (4 bins, 8 Hz at the default size and 8 kS/s): the safe direction for a limit.
 *     The same smoothing averages a single narrow spur with its neighbours, so it is
 *     seen about 8 dB weaker than it is. This measures the skirts of the modulation;
 *     discrete spurs from the radio need a spectrum analyser.
 */

#include <complex>
#include <cstddef>
#include <vector>

namespace mpsk::dsp {

/**
 * In-place radix-2 FFT (decimation in time). The size must be a power of two.
 * The inverse transform includes the 1/N scaling.
 * @throws std::invalid_argument if the size is not a power of two (zero and one are allowed).
 */
void fft(std::vector<std::complex<double>>& data, bool inverse = false);

struct SpectrumConfig {
  std::size_t fft_size = 4096;   ///< power of two, at least 64
  std::size_t smoothing_bins = 9; ///< odd; the skirt measurement averages this many bins
};

struct OccupiedBandwidth {
  double power_99_hz = 0.0;      ///< narrowest symmetric band holding 99% of the power
  double power_999_hz = 0.0;     ///< ... 99.9%
  double width_26_db_hz = 0.0;   ///< full width at 26 dB below the in-band level
  double width_40_db_hz = 0.0;   ///< ... 40 dB
  double resolution_hz = 0.0;    ///< bin spacing, sample rate / fft size
  std::size_t segments = 0;      ///< periodograms averaged
};

/**
 * Welch power spectrum, full band, in FFT bin order (bin k is k * fs / N, with the
 * upper half meaning negative frequencies). Linear power, arbitrary scale.
 * @throws std::invalid_argument if the record is shorter than the FFT size or the
 *         size is not a power of two of at least 64.
 */
std::vector<double> welch_psd(const std::vector<std::complex<float>>& x, const SpectrumConfig& config,
                              std::size_t* segments_used = nullptr);

/**
 * Measures the occupied bandwidth of @p x.
 *
 * @param sample_rate_hz  Sample rate of @p x.
 * @param symbol_rate_hz  Sets the in-band reference region (0.3 of it either side of 0 Hz).
 * @throws std::invalid_argument for a bad configuration, a record shorter than the FFT
 *         size, or a rate that is not positive and finite.
 */
OccupiedBandwidth measure_occupied_bandwidth(const std::vector<std::complex<float>>& x, double sample_rate_hz,
                                             double symbol_rate_hz, const SpectrumConfig& config = SpectrumConfig{});

}  // namespace mpsk::dsp
