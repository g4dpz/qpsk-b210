#pragma once

/**
 * @file sweep.hpp
 * @brief Bit error rate against Eb/N0 through the whole simulated link (requirement 6.8).
 *
 * Each point sends batches of frames through LoopbackLink until it has seen enough
 * bit errors to trust the rate, or has sent as many bits as it is allowed. Seeds are
 * fixed by the configuration, so a sweep gives the same numbers every time on the
 * same machine and library.
 *
 * What a point can say depends on how many errors it saw. About 100 errors pin the
 * rate to within roughly 10% (one standard deviation), which is 0.2 dB of Eb/N0 at the
 * BERs of interest. A point that hit the bit cap with far fewer errors has not
 * measured the rate, only bounded it, and `reached_target` says which case it is.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "sim/channel.hpp"
#include "sim/loopback.hpp"

namespace mpsk::sim {

struct SweepPoint;

struct SweepConfig {
  LoopbackConfig link;

  /**
   * Impairments held fixed across the sweep: delay, gain, offsets, drift. Its esn0_db
   * and seed are replaced for each point.
   */
  ChannelConfig channel;

  std::vector<double> ebn0_db = {0, 2, 4, 6, 8, 10, 12};

  std::size_t target_errors = 100;    ///< stop a point at this many bit errors
  std::size_t max_bits = 1000000;     ///< ... or after sending this many payload bits
  std::size_t frames_per_run = 50;    ///< frames in one record; 1 to 5000
  std::uint64_t seed = 1;

  /**
   * Called with each point as soon as it finishes, in order, so a long sweep can show
   * progress. Optional. An exception it throws ends the sweep and reaches the caller.
   */
  std::function<void(const SweepPoint&)> on_point;
};

struct SweepPoint {
  double ebn0_db = 0.0;
  double esn0_db = 0.0;
  std::size_t runs = 0;
  std::size_t frames_sent = 0;
  std::size_t frames_delivered = 0;
  std::size_t frames_payload_ok = 0;
  std::size_t bits_sent = 0;
  std::size_t bits_compared = 0;
  std::size_t bit_errors = 0;
  bool reached_target = false;   ///< stopped on the error count rather than the bit cap
  double theory_ber = 0.0;

  double ber() const {
    return bits_compared == 0 ? 0.0 : static_cast<double>(bit_errors) / static_cast<double>(bits_compared);
  }

  /** Fraction of the frames sent that were not delivered. */
  double frame_loss() const {
    return frames_sent == 0 ? 0.0 : 1.0 - static_cast<double>(frames_delivered) / static_cast<double>(frames_sent);
  }

  /**
   * How many dB more Eb/N0 the receiver needed than ideal theory to reach this BER:
   * the point's Eb/N0 minus the Eb/N0 at which theory gives the measured BER. Positive
   * is worse than theory. std::nullopt if no bit errors were seen (no BER to invert).
   */
  std::optional<double> loss_db(int modulation_order) const;
};

/**
 * Noise seed for run @p run of point @p point (both counted from 0). Different for
 * every (point, run) pair, so no two runs share a noise realisation, and moved by
 * @p base_seed. Unique while a point has fewer than 1 000 003 runs.
 */
std::uint64_t sweep_run_seed(std::uint64_t base_seed, std::size_t point, std::size_t run);

/**
 * Counter of the first frame of a run, given how many frames the point has sent so
 * far. The counter selects the PRBS payload, so runs send different data. Cycles
 * through 1 to 50000, which leaves room for runs of up to 5000 frames inside 16 bits.
 */
std::uint16_t sweep_first_counter(std::size_t frames_sent_before);

/**
 * @throws std::invalid_argument for an empty point list, a zero target or bit cap,
 *         frames_per_run outside 1 to 5000, or a link or channel LoopbackLink rejects.
 */
std::vector<SweepPoint> run_sweep(const SweepConfig& config);

}  // namespace mpsk::sim
