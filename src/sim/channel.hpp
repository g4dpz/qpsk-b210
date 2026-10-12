#pragma once

/**
 * @file channel.hpp
 * @brief The simulated radio channel: everything between the transmitter's output and
 *        the receiver's input, in the order the design gives.
 *
 *   1. bulk delay            whole and fractional samples; may exceed a frame
 *   2. amplitude scale       gain in dB
 *   3. carrier offset        frequency offset and a starting phase
 *   4. slow phase drift      a frequency ramp, plus a random-walk phase
 *   5. sample-clock offset   the receiver's sample clock off by some ppm
 *   6. white Gaussian noise  at a requested symbol SNR
 *
 * Nothing else is simulated: no multipath, no fading, no I/Q imbalance, no
 * converter clipping. Steps 3 and 4 are one phase track applied in one pass.
 *
 * The order matters. The delay comes first, so the clock offset (which stretches
 * time from the start of the record) acts on the delayed signal and a signal
 * delayed by D samples arrives at D * (1 + ppm * 1e-6). The gain comes before the
 * noise, and the noise level is set against the signal as it arrives, so Es/N0 is
 * what the receiver sees whatever the gain: a signal 40 dB down has noise 40 dB
 * down with it. A fixed noise floor under a falling signal is not simulated.
 */

#include <complex>
#include <cstdint>
#include <optional>
#include <vector>

namespace mpsk::sim {

struct ChannelConfig {
  /** Sample rate in Hz. Turns the frequency settings into cycles per sample. */
  double sample_rate_hz = 1e6;

  /** Samples per symbol of the signal. Needed only to set the noise from Es/N0. */
  int samples_per_symbol = 4;

  /**
   * Bulk delay in samples, at least 0, not necessarily whole. The record grows by
   * the delay: it is the signal preceded by that much silence.
   */
  double delay_samples = 0.0;

  /** Amplitude scale in dB (20 log10 of the amplitude ratio). Negative attenuates. */
  double gain_db = 0.0;

  /** Carrier frequency offset in Hz. */
  double cfo_hz = 0.0;

  /** Carrier phase at the first sample of the delayed record, radians. */
  double phase_rad = 0.0;

  /**
   * Slow drift, part one: the carrier frequency changes linearly at this rate, in
   * hertz per second, starting from cfo_hz. The phase then has a quadratic term.
   * (A ramp in phase is just a frequency offset, which cfo_hz already is, so the
   * ramp that is a different impairment is the ramp in frequency.)
   */
  double freq_drift_hz_per_s = 0.0;

  /**
   * Slow drift, part two: the phase also does a random walk, a Wiener process whose
   * increments over t seconds have a standard deviation of this value times
   * sqrt(t). Radians per root second. Oscillator phase noise at low offset
   * frequencies looks like this.
   */
  double phase_walk_rad_per_sqrt_s = 0.0;

  /**
   * Receiver sample-clock offset in parts per million. Positive means the
   * receiver's clock runs fast (see apply_clock_offset()).
   */
  double clock_offset_ppm = 0.0;

  /**
   * Symbol SNR at the receiver, dB. std::nullopt adds no noise. Measured against the
   * signal power after the gain; see apply().
   */
  std::optional<double> esn0_db;

  /** Seed for the noise and the phase random walk. The same seed gives the same output. */
  std::uint64_t seed = 1;
};

class Channel {
 public:
  /**
   * @throws std::invalid_argument for a configuration that cannot be simulated: a
   *         non-positive sample rate or samples per symbol, a negative or non-finite
   *         delay, a non-finite setting, a negative random-walk strength, a clock
   *         offset that makes the sample period non-positive.
   */
  explicit Channel(const ChannelConfig& config);

  /**
   * Passes a record through the channel.
   *
   * @param x                 The transmitter's output.
   * @param signal_power      Mean power per sample of @p x while a burst is on the air
   *                          (not counting guard silence), before the gain. For the
   *                          modulator that is output_scale^2 / sps. Used only to set
   *                          the noise; ignored when the config has no esn0_db.
   * @return The received record. Longer than @p x by the delay and by the clock
   *         offset's stretch. The same call always gives the same result.
   * @throws std::invalid_argument if noise is requested and @p signal_power is not
   *         positive and finite.
   */
  std::vector<std::complex<float>> apply(const std::vector<std::complex<float>>& x,
                                         double signal_power = 0.0) const;

  const ChannelConfig& config() const { return config_; }

 private:
  ChannelConfig config_;
};

}  // namespace mpsk::sim
