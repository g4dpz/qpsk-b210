#include "dsp/receiver.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "dsp/framer.hpp"

namespace mpsk::dsp {

ReceiverConfig ReceiverConfig::for_pulse(int samples_per_symbol, double rolloff, int span_symbols) {
  ReceiverConfig c;
  c.front_end.samples_per_symbol = samples_per_symbol;
  c.front_end.rolloff = rolloff;
  c.front_end.span_symbols = span_symbols;
  c.acquisition.samples_per_symbol = samples_per_symbol;
  c.demod.samples_per_symbol = samples_per_symbol;
  c.demod.rolloff = rolloff;
  c.demod.span_symbols = span_symbols;
  return c;
}

namespace {

const ReceiverConfig& validated(const ReceiverConfig& c) {
  const bool sps_ok = c.front_end.samples_per_symbol == c.acquisition.samples_per_symbol &&
                      c.front_end.samples_per_symbol == c.demod.samples_per_symbol;
  const bool pulse_ok = c.front_end.rolloff == c.demod.rolloff &&
                        c.front_end.span_symbols == c.demod.span_symbols;
  if (!sps_ok || !pulse_ok) {
    throw std::invalid_argument(
        "Receiver: the front end, acquisition and demodulator disagree on the pulse shape "
        "(samples per symbol " + std::to_string(c.front_end.samples_per_symbol) + "/" +
        std::to_string(c.acquisition.samples_per_symbol) + "/" +
        std::to_string(c.demod.samples_per_symbol) + ", roll-off " +
        std::to_string(c.front_end.rolloff) + "/" + std::to_string(c.demod.rolloff) + ", span " +
        std::to_string(c.front_end.span_symbols) + "/" + std::to_string(c.demod.span_symbols) +
        "); build the configuration with ReceiverConfig::for_pulse()");
  }
  return c;
}

}  // namespace

Receiver::Receiver(const ReceiverConfig& config)
    : config_(validated(config)),
      demodulator_(config.demod),
      // Building a front end also validates its settings, so a bad configuration fails
      // here and not on the first record.
      group_delay_(RxFrontEnd(config.front_end).group_delay()) {}

int Receiver::group_delay() const { return group_delay_; }

std::vector<ReceivedFrame> Receiver::receive(const std::vector<std::complex<float>>& raw,
                                             AcquisitionStats* stats) const {
  RxFrontEnd front_end(config_.front_end);
  const std::vector<std::complex<float>> y = front_end.process(raw);
  const std::size_t sps = static_cast<std::size_t>(config_.front_end.samples_per_symbol);

  if (stats) *stats = AcquisitionStats{};
  std::vector<ReceivedFrame> frames;
  std::size_t start = 0;
  while (start < y.size()) {
    AcquisitionStats scan;
    const auto estimate = acquire_preamble(y.data(), y.size(), start, config_.acquisition, &scan);
    if (stats) {
      stats->positions_scanned += scan.positions_scanned;
      stats->candidates += scan.candidates;
      stats->rejected += scan.rejected;
      if (scan.max_rejected_verify > stats->max_rejected_verify) {
        stats->max_rejected_verify = scan.max_rejected_verify;
      }
    }
    if (!estimate) break;

    ReceivedFrame f;
    f.estimate = *estimate;
    f.demod = demodulator_.demodulate(y.data(), y.size(), *estimate);

    // Where to resume. The first symbol's pulse peaks at estimate.position, the last
    // symbol of the frame's n symbols peaks (n - 1) symbols later, and the next frame
    // cannot start before that.
    std::size_t known_symbols;
    if (f.demod.frame.header_ok) {
      known_symbols = frame_symbol_count(f.demod.frame.header.type,
                                         f.demod.frame.header.modulation_order,
                                         f.demod.frame.header.payload_length);
    } else {
      known_symbols = static_cast<std::size_t>(kPreambleSymbols + kHeaderSymbols);
    }
    const double resume = std::floor(estimate->position) + static_cast<double>((known_symbols - 1) * sps) + 1.0;
    const std::size_t next = static_cast<std::size_t>(resume);
    start = next > start ? next : start + 1;   // always make progress
    frames.push_back(std::move(f));
  }
  return frames;
}

}  // namespace mpsk::dsp
