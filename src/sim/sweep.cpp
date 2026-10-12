#include "sim/sweep.hpp"

#include <stdexcept>
#include <string>

#include "sim/theory.hpp"

namespace mpsk::sim {

std::optional<double> SweepPoint::loss_db(int modulation_order) const {
  if (bit_errors == 0 || bits_compared == 0) return std::nullopt;
  const double rate = ber();
  if (!(rate < 0.5)) return std::nullopt;   // no better than guessing: the curve has no inverse here
  return ebn0_db - mpsk_ebn0_for_ber(modulation_order, rate);
}

std::uint64_t sweep_run_seed(std::uint64_t base_seed, std::size_t point, std::size_t run) {
  return base_seed + 1000003ULL * (point + 1) + run;
}

std::uint16_t sweep_first_counter(std::size_t frames_sent_before) {
  return static_cast<std::uint16_t>(1 + (frames_sent_before % 50000));
}

std::vector<SweepPoint> run_sweep(const SweepConfig& config) {
  if (config.ebn0_db.empty()) throw std::invalid_argument("run_sweep: no Eb/N0 points given");
  if (config.target_errors == 0 || config.max_bits == 0) {
    throw std::invalid_argument("run_sweep: target_errors and max_bits must be positive");
  }
  if (config.frames_per_run < 1 || config.frames_per_run > 5000) {
    throw std::invalid_argument("run_sweep: frames_per_run must be 1 to 5000, got " +
                                std::to_string(config.frames_per_run));
  }

  // One link for the whole sweep: building the receiver measures the timing detector's
  // gain, which takes a noticeable fraction of a second in an unoptimised build.
  const LoopbackLink link(config.link);
  const int order = config.link.modulator.modulation_order;

  std::vector<SweepPoint> points;
  for (std::size_t p = 0; p < config.ebn0_db.size(); ++p) {
    SweepPoint pt;
    pt.ebn0_db = config.ebn0_db[p];
    pt.esn0_db = ebn0_to_esn0_db(order, pt.ebn0_db);
    pt.theory_ber = mpsk_ber_theory(order, pt.ebn0_db);

    ChannelConfig ch = config.channel;
    ch.esn0_db = pt.esn0_db;

    while (pt.bit_errors < config.target_errors && pt.bits_sent < config.max_bits) {
      ch.seed = sweep_run_seed(config.seed, p, pt.runs);
      const std::uint16_t first_counter = sweep_first_counter(pt.frames_sent);
      const LoopbackResult r = link.run(ch, config.frames_per_run, first_counter);

      ++pt.runs;
      pt.frames_sent += r.frames_sent;
      pt.frames_delivered += r.frames_delivered;
      pt.frames_payload_ok += r.frames_payload_ok;
      pt.bits_sent += r.bits_sent;
      pt.bits_compared += r.bits_compared;
      pt.bit_errors += r.bit_errors;
    }
    pt.reached_target = pt.bit_errors >= config.target_errors;
    points.push_back(pt);
  }
  return points;
}

}  // namespace mpsk::sim
