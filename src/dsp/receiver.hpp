#pragma once

/**
 * @file receiver.hpp
 * @brief Finds and decodes every frame in a recorded block of samples.
 *
 *   raw samples -> front end (DC blocker, matched filter)
 *     -> acquire_preamble() -> Demodulator -> next frame ...
 *
 * This is the block form of the receiver: it is handed a record and returns all
 * the frames in it. The offline loopback and the file-based `rx` use it directly.
 * A live radio feeds it a window of recent samples in the same way.
 *
 * After a frame the scan resumes where that frame ends, so its payload (scrambled,
 * pseudorandom-looking) is not searched for a preamble. When the header cannot be
 * decoded the frame's length is unknown, so the scan resumes after the preamble and
 * header, which are the only part known to be there.
 */

#include <complex>
#include <cstddef>
#include <vector>

#include "dsp/demodulator.hpp"
#include "dsp/preamble_detector.hpp"
#include "dsp/rx_frontend.hpp"

namespace mpsk::dsp {

struct ReceiverConfig {
  RxFrontEndConfig front_end;
  AcquisitionConfig acquisition;
  DemodConfig demod;

  /**
   * A configuration whose three parts agree on the pulse shape. They must: the matched
   * filter, the acquisition and the loops all work in terms of the same samples per
   * symbol, roll-off and span, and the Receiver constructor refuses a mismatch.
   */
  static ReceiverConfig for_pulse(int samples_per_symbol, double rolloff, int span_symbols);
};

/** One frame found in a record. */
struct ReceivedFrame {
  /**
   * What the preamble gave. estimate.position is an index into the front end's output,
   * which is the input delayed by Receiver::group_delay() samples.
   */
  PreambleEstimate estimate;
  DemodResult demod;
};

class Receiver {
 public:
  /** @throws std::invalid_argument if the parts disagree on the pulse shape, or any part is unusable. */
  explicit Receiver(const ReceiverConfig& config);

  /**
   * Finds every frame in @p raw, in order of arrival.
   *
   * A frame is reported when its preamble passes detection, whether or not the header
   * then decodes: check demod.lost(). A frame cut off by the end of the record is
   * reported with demod.truncated set.
   *
   * @param stats  If not null, receives the acquisition counters added up over the whole scan.
   */
  std::vector<ReceivedFrame> receive(const std::vector<std::complex<float>>& raw,
                                     AcquisitionStats* stats = nullptr) const;

  /** Delay of the front end in samples: add it to a transmit-side position to get an estimate.position. */
  int group_delay() const;

  const ReceiverConfig& config() const { return config_; }

 private:
  ReceiverConfig config_;
  Demodulator demodulator_;   ///< built once: it measures the timing detector's gain
  int group_delay_;
};

}  // namespace mpsk::dsp
