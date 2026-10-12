#pragma once

/**
 * @file rx_scenario.hpp
 * @brief Builds a received record from a transmitted burst, with the ground truth.
 *
 * One burst goes through, in this order: a leading stretch of silence, a
 * fractional delay, a carrier frequency and phase offset, a gain, a DC offset,
 * and white Gaussian noise at a requested symbol SNR. The record then goes
 * through the receive front end. The result carries what the receiver should
 * measure, so a test can compare the two.
 *
 * Conventions that the ground truth follows (the same as preamble_detector.hpp):
 *   - position: input index of the pulse peak of the first preamble symbol, after
 *     the matched filter (so including both filters' delay);
 *   - phase_centre: carrier phase at the middle of the preamble, symbol 31.5;
 *   - cfo is in Hz at the given sample rate.
 */

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "dsp/rx_frontend.hpp"
#include "sim/channel_ops.hpp"

namespace rx_test {

using cf = std::complex<float>;

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 1e6;   // 1 MS/s
constexpr int kSps = 4;
constexpr double kSymbolRate = kSampleRate / kSps;  // 250 ksym/s

struct Scenario {
  double esn0_db = 100.0;    ///< symbol SNR; 100 means effectively noiseless
  double cfo_hz = 0.0;
  double phase_rad = 0.0;
  double delay = 0.0;        ///< extra fractional delay, samples
  double gain = 1.0;         ///< linear, applied to signal and DC offset
  cf dc = cf(0.0f, 0.0f);    ///< DC offset, before the gain
  std::size_t lead = 400;    ///< silence before the burst, samples
  std::size_t tail = 400;    ///< silence after the burst, samples
  std::uint64_t seed = 1;
  bool add_noise = true;
};

struct Received {
  std::vector<cf> y;         ///< front-end output
  double true_position = 0;  ///< see the file comment
  double true_phase_centre = 0;
  double true_cfo_hz = 0;
  double true_amplitude = 0; ///< amplitude of a unit symbol at the receiver
  std::size_t burst_samples = 0;
};

/** The modulator configuration these scenarios use: QPSK, 4 sps, the defaults. */
inline mpsk::dsp::Modulator& modulator() {
  static mpsk::dsp::Modulator m{mpsk::dsp::ModulatorConfig{}};
  return m;
}

inline Received make_received(const Scenario& s, std::uint16_t frame_counter = 1,
                              std::size_t payload_bytes = 16) {
  using namespace mpsk;
  const dsp::ModulatedBurst burst =
      modulator().modulate(dsp::FrameType::Data, frame_counter,
                           dsp::prbs_bytes(3, frame_counter, payload_bytes));

  std::vector<cf> x(s.lead, cf(0, 0));
  x.insert(x.end(), burst.samples.begin(), burst.samples.end());
  x.insert(x.end(), s.tail, cf(0, 0));

  std::vector<cf> r = sim::delay_signal(x, s.delay);
  sim::rotate_carrier(r, s.cfo_hz / kSampleRate, s.phase_rad);
  for (cf& v : r) v = (v + s.dc) * static_cast<float>(s.gain);

  if (s.add_noise && s.esn0_db < 99.0) {
    sim::NoiseSource rng(s.seed);
    // On-air power per sample, with the gain applied: output scale^2 / sps, times gain^2.
    const double scale = modulator().config().output_scale;
    const double signal_power = (scale * s.gain) * (scale * s.gain) / kSps;
    sim::add_noise(r, sim::noise_variance_for_esn0(signal_power, kSps, s.esn0_db), rng);
  }

  dsp::RxFrontEnd fe{dsp::RxFrontEndConfig{}};
  Received out;
  out.y = fe.process(r);
  out.burst_samples = burst.samples.size();

  // Where the first preamble symbol's pulse peaks after both filters.
  const double tx_peak = static_cast<double>(s.lead) + s.delay + static_cast<double>(burst.first_symbol_peak_index);
  out.true_position = tx_peak + static_cast<double>(fe.group_delay());

  // Carrier phase at the middle of the preamble: the carrier at the time of that
  // symbol's pulse peak in the received timeline (before the receive filter's delay).
  out.true_phase_centre =
      s.phase_rad + 2.0 * kPi * (s.cfo_hz / kSampleRate) * (tx_peak + 31.5 * kSps);
  out.true_cfo_hz = s.cfo_hz;
  out.true_amplitude = s.gain * modulator().config().output_scale;
  return out;
}

/** Phase difference wrapped to (-pi, pi]. */
inline double wrap_phase(double a) { return std::remainder(a, 2.0 * kPi); }

}  // namespace rx_test
