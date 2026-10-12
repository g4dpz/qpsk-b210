#pragma once

/**
 * @file modulator.hpp
 * @brief Transmit chain: frame bits to shaped, scaled baseband samples.
 *
 *   payload bytes -> frame (preamble, header, payload section) -> symbols
 *     -> upsample by sps and RRC filter -> scale -> add guard silence
 *
 * The output is a complete burst, ready to hand to the radio or to write to a
 * file. Guard silence at both ends gives the transmit chain time to settle and
 * lets the receiver see noise alone just before and after the frame.
 */

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/framer.hpp"

namespace mpsk::dsp {

struct ModulatorConfig {
  int modulation_order = 4;      ///< payload M. QPSK in stage 1.
  int samples_per_symbol = 4;
  double rolloff = 0.35;
  int span_symbols = 11;         ///< RRC span. samples_per_symbol * span_symbols must be even.

  /**
   * Fixed multiplier applied to the shaped signal. The RRC taps have unit
   * energy and the symbols unit power, so the shaped signal has an average
   * power of 1/sps; the output power is therefore scale^2 / sps. A fixed scale
   * (instead of normalising each burst to its own peak) keeps the transmitted
   * power the same from frame to frame, which the receiver's level reports and
   * the BER-versus-SNR comparisons rely on. Must be in (0, 1].
   */
  double output_scale = 0.5;

  /**
   * Samples of silence before and after the shaped burst. The default of 64 is
   * 16 symbols at 4 samples per symbol.
   */
  int guard_samples = 64;
};

struct ModulatedBurst {
  std::vector<std::complex<float>> samples;

  /**
   * Index in @p samples where the pulse of the first preamble symbol peaks:
   * guard_samples plus the RRC group delay. The echo timing is defined from this
   * point, not from sample 0, so the guard length does not affect ranging.
   */
  std::size_t first_symbol_peak_index = 0;

  std::size_t symbol_count = 0;  ///< symbols in the frame, before upsampling

  /**
   * Largest sample magnitude. The radio's converter clips each of I and Q at
   * +-1, and a component is never larger than the magnitude, so a peak at or
   * below 1 guarantees no clipping.
   */
  float peak = 0.0f;
};

class Modulator {
 public:
  /**
   * @throws std::invalid_argument for an unusable configuration: an unsupported
   *         or BPSK payload modulation (the message says what is enabled), bad
   *         RRC parameters (see rrc_taps), an output scale outside (0, 1], or a
   *         negative guard.
   */
  explicit Modulator(const ModulatorConfig& config);

  /**
   * Modulates one frame.
   * @param trailer  Required for echo frames and forbidden for data frames.
   * @throws std::invalid_argument as for build_frame_symbols().
   */
  ModulatedBurst modulate(FrameType type, uint16_t frame_counter,
                          const std::vector<uint8_t>& payload,
                          const EchoTrailer* trailer = nullptr) const;

  /** Length in samples of the burst modulate() returns, including both guards. */
  std::size_t burst_samples(FrameType type, std::size_t payload_length) const;

  const ModulatorConfig& config() const { return config_; }

  /** The RRC taps in use, for building the matching receive filter. */
  const std::vector<float>& taps() const { return taps_; }

  /** Where the first symbol's pulse peaks within a burst: guard + group delay. */
  std::size_t first_symbol_peak_offset() const;

 private:
  ModulatorConfig config_;
  std::vector<float> taps_;
};

}  // namespace mpsk::dsp
