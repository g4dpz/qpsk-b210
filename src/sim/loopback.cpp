#include "sim/loopback.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "dsp/framer.hpp"
#include "dsp/metrics.hpp"
#include "dsp/prbs.hpp"

namespace mpsk::sim {

namespace {

dsp::ReceiverConfig resolve_receiver(const LoopbackConfig& c) {
  const dsp::ModulatorConfig& m = c.modulator;
  if (!c.receiver) {
    return dsp::ReceiverConfig::for_pulse(m.samples_per_symbol, m.rolloff, m.span_symbols);
  }
  const dsp::RxFrontEndConfig& f = c.receiver->front_end;
  if (f.samples_per_symbol != m.samples_per_symbol || f.rolloff != m.rolloff ||
      f.span_symbols != m.span_symbols) {
    throw std::invalid_argument(
        "LoopbackLink: the receiver's pulse shape (" + std::to_string(f.samples_per_symbol) +
        " samples per symbol, roll-off " + std::to_string(f.rolloff) + ", span " +
        std::to_string(f.span_symbols) + ") differs from the modulator's (" +
        std::to_string(m.samples_per_symbol) + ", " + std::to_string(m.rolloff) + ", " +
        std::to_string(m.span_symbols) + "); a matched filter has to match");
  }
  return *c.receiver;
}

}  // namespace

LoopbackLink::LoopbackLink(const LoopbackConfig& config)
    : config_(config), modulator_(config.modulator), receiver_(resolve_receiver(config)) {
  if (config.payload_bytes == 0 || config.payload_bytes > dsp::kMaxPayloadBytes) {
    throw std::invalid_argument("LoopbackLink: payload_bytes must be 1 to " +
                                std::to_string(dsp::kMaxPayloadBytes) + ", got " +
                                std::to_string(config.payload_bytes));
  }
}

LoopbackResult LoopbackLink::run(const ChannelConfig& channel_config, std::size_t frames,
                                 std::uint16_t first_counter, bool keep_samples) const {
  if (frames == 0) throw std::invalid_argument("LoopbackLink::run: frames must be at least 1");
  if (static_cast<std::size_t>(first_counter) + frames - 1 > 65535) {
    throw std::invalid_argument("LoopbackLink::run: " + std::to_string(frames) +
                                " frames from counter " + std::to_string(first_counter) +
                                " would pass the 16-bit frame counter's limit of 65535");
  }

  // ---- Transmit: one record holding every frame ---------------------------
  using cf = std::complex<float>;
  std::vector<cf> tx(config_.lead_samples, cf(0, 0));
  for (std::size_t i = 0; i < frames; ++i) {
    const std::uint16_t counter = static_cast<std::uint16_t>(first_counter + i);
    const dsp::ModulatedBurst burst = modulator_.modulate(
        dsp::FrameType::Data, counter, dsp::prbs_bytes(config_.prbs_seed, counter, config_.payload_bytes));
    tx.insert(tx.end(), burst.samples.begin(), burst.samples.end());
    tx.insert(tx.end(), i + 1 < frames ? config_.gap_samples : config_.tail_samples, cf(0, 0));
  }

  // ---- Channel ------------------------------------------------------------
  ChannelConfig cc = channel_config;
  cc.samples_per_symbol = config_.modulator.samples_per_symbol;
  // Mean power per sample while a burst is on the air: the shaped symbols have unit
  // power per symbol, so 1/sps per sample, times the output scale squared.
  const double scale = config_.modulator.output_scale;
  const double signal_power = scale * scale / static_cast<double>(config_.modulator.samples_per_symbol);
  const std::vector<cf> rx = Channel(cc).apply(tx, signal_power);

  // ---- Receive and account ------------------------------------------------
  const std::vector<dsp::ReceivedFrame> found = receiver_.receive(rx);

  LoopbackResult out;
  out.frames_sent = frames;
  out.bits_sent = frames * config_.payload_bytes * 8;
  out.frames_detected = found.size();
  std::unordered_set<std::uint16_t> seen;
  const double symbol_rate = cc.sample_rate_hz / static_cast<double>(cc.samples_per_symbol);

  for (const dsp::ReceivedFrame& rf : found) {
    FrameReport r;
    const dsp::DecodedFrame& f = rf.demod.frame;
    r.header_ok = f.header_ok;
    r.payload_crc_ok = f.payload_crc_ok;
    r.truncated = rf.demod.truncated;
    r.evm = rf.demod.evm;
    r.evm_snr_db = rf.demod.evm_snr_db;
    r.cfo_hz = rf.demod.cfo_hz(symbol_rate);
    r.clock_offset_ppm = rf.demod.clock_offset_ppm;
    r.position = rf.estimate.position;

    if (f.header_ok) {
      r.counter = f.header.frame_counter;
      const bool was_sent = r.counter >= first_counter && r.counter < first_counter + frames;
      const bool new_counter = seen.insert(r.counter).second;
      if (was_sent && new_counter) {
        const dsp::BerResult ber = dsp::payload_ber(
            f.payload, dsp::prbs_bytes(config_.prbs_seed, r.counter, config_.payload_bytes));
        // A frame whose header passed its CRC but whose length differs from what was
        // sent is not the frame we sent, whatever its counter says.
        if (!ber.length_mismatch) {
          r.delivered = true;
          r.bit_errors = ber.bit_errors;
          r.bits_compared = ber.bits_compared;
          out.bit_errors += ber.bit_errors;
          out.bits_compared += ber.bits_compared;
          ++out.frames_delivered;
          if (f.payload_crc_ok) ++out.frames_payload_ok;
        }
      }
    }
    out.frames.push_back(r);
  }
  out.frames_lost = frames - out.frames_delivered;

  if (keep_samples) {
    out.transmitted = tx;
    out.received = rx;
  }
  return out;
}

}  // namespace mpsk::sim
