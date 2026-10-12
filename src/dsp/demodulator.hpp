#pragma once

/**
 * @file demodulator.hpp
 * @brief Recovers a frame's header and payload from the matched-filter output.
 *
 * Input: the output of RxFrontEnd, and the PreambleEstimate that acquire_preamble()
 * made for it. From there:
 *
 *   1. The loops start at the last preamble symbol, with the carrier phase and
 *      frequency and the sampling position that the preamble gave.
 *   2. For each following symbol: read the signal at the loop's sampling position,
 *      and half a symbol earlier, with the cubic Farrow interpolator; divide by the
 *      preamble's amplitude estimate; remove the carrier phase; decide the nearest
 *      constellation point; and update both loops.
 *   3. The 72 header symbols are BPSK. They are decoded and the header's CRC is
 *      checked. If it fails the frame is lost: nothing more is decoded.
 *   4. The header gives the frame type, the payload modulation and its length, so
 *      the payload symbols are read with that constellation, and the payload
 *      section is unpacked and its CRC-32 checked.
 *
 * Decision order for the loops (the design): BPSK decisions for the header, then
 * M-PSK decisions for the payload.
 *
 * Failures are reported in the result and never thrown. A frame that cannot be
 * decoded is an expected event on a radio link.
 */

#include <complex>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "dsp/carrier_loop.hpp"
#include "dsp/framer.hpp"
#include "dsp/preamble_detector.hpp"
#include "dsp/timing_loop.hpp"

namespace mpsk::dsp {

struct DemodConfig {
  int samples_per_symbol = 4;
  double rolloff = 0.35;
  int span_symbols = 11;

  CarrierLoopConfig carrier;   ///< default noise bandwidth 0.01, damping 0.7071
  double timing_bn_t = 0.003;  ///< timing loop noise bandwidth; 0 freezes the timing loop
  double timing_damping = 0.70710678118654752;

  /**
   * Gardner detector gain per symbol for this pulse shape. 0 means measure it when
   * the demodulator is built. See TimingLoop::measure_detector_gain().
   */
  double timing_detector_gain = 0.0;
};

struct DemodResult {
  /** The decoded frame. Valid fields depend on how far decoding got; see DecodedFrame. */
  DecodedFrame frame;

  /** True if the frame is lost: the header could not be decoded, so nothing else was. */
  bool lost() const { return !frame.header_ok; }

  /** True if the buffer ended before the whole frame was read. */
  bool truncated = false;

  // ---- Measurements -------------------------------------------------------

  /** EVM over the payload symbols, as a fraction. 0 if no payload symbol was decoded. */
  double evm = 0.0;

  /** Symbol SNR estimated from the EVM, dB. +infinity for an EVM of exactly 0. */
  double evm_snr_db = 0.0;

  /** Number of payload symbols the EVM covers. */
  std::size_t evm_symbols = 0;

  /**
   * Carrier frequency offset, radians per symbol: the preamble's coarse estimate
   * plus whatever the loop learned, averaged over the payload (or over the header,
   * if the payload was not reached).
   */
  double omega = 0.0;

  /** The same in hertz, for the symbol rate in AcquisitionConfig-compatible units. */
  double cfo_hz(double symbol_rate_hz) const { return omega * symbol_rate_hz / (2.0 * 3.14159265358979323846); }

  /**
   * The sample-clock offset the timing loop learned, ppm, averaged over the payload
   * (or the header, if the payload was not reached). Averaging matters: on a signal
   * with no drift the instantaneous reading wanders by about 8 ppm rms at the default
   * loop bandwidth, so any single reading is useless below roughly 20 ppm, and even
   * the average only settles on a long frame. Measured on a 4096-byte frame the
   * average reads 16, 52, 97, 197 and 400 ppm for true offsets of 20, 50, 100, 200
   * and 400. Treat it as a diagnostic that large offsets show up in, not as a
   * measurement of the few ppm a disciplined reference leaves.
   */
  double clock_offset_ppm = 0.0;

  /** Symbols decoded after the preamble (header plus payload). */
  std::size_t symbols_decoded = 0;

  /** The corrected, unit-amplitude header and payload symbols, for plots and tests. */
  std::vector<std::complex<float>> header_symbols;
  std::vector<std::complex<float>> payload_symbols;
};

class Demodulator {
 public:
  /** @throws std::invalid_argument for an unusable configuration. */
  explicit Demodulator(const DemodConfig& config);

  /**
   * Decodes the frame whose preamble was found at @p estimate in @p y.
   *
   * @param y         Front-end output, @p n samples.
   * @param estimate  From acquire_preamble() or estimate_preamble() for this buffer.
   */
  DemodResult demodulate(const std::complex<float>* y, std::size_t n,
                         const PreambleEstimate& estimate) const;

  const DemodConfig& config() const { return config_; }

 private:
  DemodConfig config_;
  double detector_gain_;   ///< measured once, here, not for every frame
};

}  // namespace mpsk::dsp
