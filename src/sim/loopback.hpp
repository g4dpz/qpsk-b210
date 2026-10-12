#pragma once

/**
 * @file loopback.hpp
 * @brief One-way offline link: modulator, simulated channel, receiver (requirement 6.1).
 *
 * A run sends a number of frames as one continuous record, with silence between
 * them, through the channel once, and hands the whole record to the receiver. One
 * record, not one channel pass per frame, so the carrier offset, the phase drift and
 * the sample-clock offset are continuous across the capture the way they are on a
 * real receiver, and the receiver has to find every frame in the stream.
 *
 * The payloads are PRBS, so the receiver side can regenerate what each frame should
 * have carried from the counter in its header and count bit errors without the
 * payload being sent separately. A frame counts as delivered when its header decodes
 * (the CRC-16 passes) and its counter is one that was sent.
 *
 * Bit errors are counted over the payload of every delivered frame, including the
 * ones whose payload CRC failed: the CRC says the frame is wrong, and the BER says
 * by how much. Frames that were not delivered contribute no bits, and are counted
 * separately. At low SNR the BER is therefore conditional on the header having
 * survived. The header and the payload see independent noise, so this changes the
 * payload's error rate very little, but it is why `frames_lost` is reported next to
 * the BER and must be read with it.
 */

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "dsp/modulator.hpp"
#include "dsp/receiver.hpp"
#include "sim/channel.hpp"

namespace mpsk::sim {

struct LoopbackConfig {
  dsp::ModulatorConfig modulator;

  /**
   * The receiver. When empty it is built to match the modulator's pulse shape with
   * every other setting at its default. Give one to try other loop bandwidths or
   * thresholds; its pulse shape must match the modulator's.
   */
  std::optional<dsp::ReceiverConfig> receiver;

  std::size_t payload_bytes = 128;   ///< per frame. 128 bytes is 1024 payload bits.
  std::uint32_t prbs_seed = 3;

  /** Silence before the first burst, between bursts, and after the last, in samples. Added to each burst's own guard. */
  std::size_t lead_samples = 256;
  std::size_t gap_samples = 256;
  std::size_t tail_samples = 512;
};

/** What the receiver made of one detected frame. */
struct FrameReport {
  /** True if the header decoded and the counter is one that was sent (and not already seen). */
  bool delivered = false;
  bool header_ok = false;
  bool payload_crc_ok = false;
  bool truncated = false;
  std::uint16_t counter = 0;          ///< from the header; meaningful if header_ok
  std::size_t bit_errors = 0;         ///< against the regenerated payload; delivered frames only
  std::size_t bits_compared = 0;
  double evm = 0.0;                   ///< fraction of the unit symbol amplitude
  double evm_snr_db = 0.0;
  double cfo_hz = 0.0;                ///< carrier offset the receiver measured
  double clock_offset_ppm = 0.0;      ///< see DemodResult::clock_offset_ppm for how far to trust it
  double position = 0.0;              ///< preamble position in the front end's output, samples
};

struct LoopbackResult {
  std::vector<FrameReport> frames;    ///< one per detection, in order of arrival

  std::size_t frames_sent = 0;
  std::size_t frames_detected = 0;    ///< preambles found, including ones whose header then failed
  std::size_t frames_delivered = 0;
  std::size_t frames_payload_ok = 0;  ///< delivered with a good payload CRC
  std::size_t frames_lost = 0;        ///< sent but not delivered
  std::size_t bit_errors = 0;
  std::size_t bits_compared = 0;
  std::size_t bits_sent = 0;          ///< payload bits transmitted, delivered or not

  /** Bit error rate over the delivered frames. 0 if none was delivered. */
  double ber() const {
    return bits_compared == 0 ? 0.0 : static_cast<double>(bit_errors) / static_cast<double>(bits_compared);
  }

  /** Filled only when the run was asked to keep them. */
  std::vector<std::complex<float>> transmitted;
  std::vector<std::complex<float>> received;
};

class LoopbackLink {
 public:
  /**
   * @throws std::invalid_argument for an unusable modulator or receiver
   *         configuration, a receiver whose pulse shape differs from the
   *         modulator's, or a payload size the framer rejects.
   */
  explicit LoopbackLink(const LoopbackConfig& config);

  /**
   * Sends @p frames frames, counters first_counter, first_counter + 1, ..., through
   * @p channel and receives them.
   *
   * The channel's samples_per_symbol is replaced by the modulator's, so the two
   * cannot disagree. Its seed decides the noise: vary it between runs for
   * independent results. This is const and re-entrant; the receiver is shared.
   *
   * @param keep_samples  Keep the transmitted and received records in the result.
   * @throws std::invalid_argument if @p frames is 0 or the counters would pass 65535.
   */
  LoopbackResult run(const ChannelConfig& channel, std::size_t frames, std::uint16_t first_counter = 1,
                     bool keep_samples = false) const;

  const LoopbackConfig& config() const { return config_; }

 private:
  LoopbackConfig config_;
  dsp::Modulator modulator_;
  dsp::Receiver receiver_;
};

}  // namespace mpsk::sim
