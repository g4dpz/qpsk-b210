#include "dsp/modulator.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "dsp/constellation.hpp"
#include "dsp/fir.hpp"
#include "dsp/rrc.hpp"

namespace mpsk::dsp {

Modulator::Modulator(const ModulatorConfig& config) : config_(config) {
  // Constellation rejects unsupported orders with a message that says what is
  // enabled (and, in stage 1, that 8-PSK is not).
  const Constellation payload_constellation(config.modulation_order);
  (void)payload_constellation;
  if (config.modulation_order == 2) {
    throw std::invalid_argument("Modulator: BPSK is not a payload modulation");
  }

  if (!(config.output_scale > 0.0 && config.output_scale <= 1.0)) {  // also rejects NaN
    throw std::invalid_argument("Modulator: output scale must be in (0, 1], got " +
                                std::to_string(config.output_scale));
  }
  if (config.guard_samples < 0) {
    throw std::invalid_argument("Modulator: guard samples must not be negative, got " +
                                std::to_string(config.guard_samples));
  }

  // Validates the roll-off, samples per symbol and span, with its own messages.
  taps_ = rrc_taps(config.rolloff, config.samples_per_symbol, config.span_symbols);
}

std::size_t Modulator::first_symbol_peak_offset() const {
  return static_cast<std::size_t>(config_.guard_samples) +
         static_cast<std::size_t>(rrc_group_delay(config_.samples_per_symbol, config_.span_symbols));
}

std::size_t Modulator::burst_samples(FrameType type, std::size_t payload_length) const {
  const std::size_t symbols = frame_symbol_count(type, config_.modulation_order, payload_length);
  const std::size_t shaped = (symbols - 1) * static_cast<std::size_t>(config_.samples_per_symbol) +
                             taps_.size();
  return 2 * static_cast<std::size_t>(config_.guard_samples) + shaped;
}

ModulatedBurst Modulator::modulate(FrameType type, uint16_t frame_counter,
                                   const std::vector<uint8_t>& payload,
                                   const EchoTrailer* trailer) const {
  const std::vector<std::complex<float>> symbols =
      build_frame_symbols(type, config_.modulation_order, frame_counter, payload, trailer);
  const std::vector<std::complex<float>> shaped =
      upsample_and_filter(symbols, taps_, config_.samples_per_symbol);

  const std::size_t guard = static_cast<std::size_t>(config_.guard_samples);
  const float scale = static_cast<float>(config_.output_scale);

  ModulatedBurst burst;
  burst.samples.assign(guard + shaped.size() + guard, std::complex<float>(0.0f, 0.0f));
  for (std::size_t i = 0; i < shaped.size(); ++i) {
    const std::complex<float> s = shaped[i] * scale;
    burst.samples[guard + i] = s;
    burst.peak = std::max(burst.peak, std::abs(s));
  }
  burst.first_symbol_peak_index = first_symbol_peak_offset();
  burst.symbol_count = symbols.size();
  return burst;
}

}  // namespace mpsk::dsp
