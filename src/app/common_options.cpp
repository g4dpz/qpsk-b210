#include "app/common_options.hpp"

#include <algorithm>
#include <cmath>

#include "dsp/constellation.hpp"

namespace mpsk::app {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

std::string modulation_name(int order) {
  switch (order) {
    case 4: return "qpsk";
    case 8: return "8psk";
    default: return std::to_string(order) + "-psk";
  }
}

dsp::ModulatorConfig PhyOptions::modulator() const {
  dsp::ModulatorConfig m;
  m.modulation_order = modulation_order;
  m.samples_per_symbol = samples_per_symbol;
  m.rolloff = rolloff;
  m.span_symbols = span_symbols;
  return m;
}

dsp::ReceiverConfig PhyOptions::receiver() const {
  dsp::ReceiverConfig r = dsp::ReceiverConfig::for_pulse(samples_per_symbol, rolloff, span_symbols);
  r.acquisition.coarse_threshold = detect_threshold;
  return r;
}

void add_phy_options(OptionParser& p) {
  p.group("Common options");
  p.add_string("mod", "", "Payload modulation", std::string("qpsk"), {"qpsk", "8psk"});
  p.add_integer("sps", "N", "Samples per symbol", 4, 2, 64);
  p.add_number("rolloff", "BETA", "Root-raised-cosine roll-off; below 0.1 the receiver does not lock", 0.35, 0.1, 1.0);
  p.add_integer("span", "N", "Pulse span in symbols", 11, 2, 64);
  p.add_integer("payload-bytes", "N", "Payload bytes per frame", 128, 1, 4096);
  p.add_integer("seed", "N", "Seed of the pseudorandom payload", 3, 0, 4294967295LL);
  p.add_number("rate", "HZ", "Sample rate; symbol rate is rate / sps", 1e6, 1.0, 1e9);
}

void add_receiver_options(OptionParser& p) {
  p.group("Receiver options");
  p.add_number("detect-threshold", "T", "Preamble detection threshold on the stage-1 statistic", 0.25, 0.01, 0.99);
}

void add_simulation_options(OptionParser& p) {
  p.group("Simulation options");
  p.add_number("ebn0", "DB", "Eb/N0 of the added noise; without it the channel is noiseless");
  p.add_number("cfo-hz", "HZ", "Carrier frequency offset", 0.0, -1e7, 1e7);
  p.add_number("phase-deg", "DEG", "Carrier phase offset at the start", 0.0, -3600.0, 3600.0);
  p.add_number("timing-frac", "F", "Fractional timing offset in samples, added to the delay", 0.0, 0.0, 1.0);
  p.add_number("delay-samples", "N", "Bulk delay in samples, whole or fractional, any length", 0.0, 0.0, 1e8);
  p.add_number("clock-ppm", "PPM", "Receiver sample-clock offset; positive means a fast clock", 0.0, -1e5, 1e5);
  p.add_number("amp-db", "DB", "Received amplitude change", 0.0, -200.0, 200.0);
  p.add_number("drift", "HZ/S", "Carrier frequency drift: the offset changes linearly at this rate", 0.0, -1e9, 1e9);
  p.add_number("phase-walk", "RAD/RT-S", "Random-walk phase strength, radians per root second", 0.0, 0.0, 1e6);
  p.add_integer("noise-seed", "N", "Seed of the noise and phase walk", 1, 0, 4294967295LL);
}

PhyOptions read_phy_options(const ParsedOptions& v) {
  PhyOptions o;
  const std::string mod = v.str("mod");
  o.modulation_order = mod == "8psk" ? 8 : 4;

  const std::vector<int>& enabled = dsp::Constellation::supported_orders();
  if (std::find(enabled.begin(), enabled.end(), o.modulation_order) == enabled.end()) {
    std::string names;
    for (const int e : enabled) names += (names.empty() ? "" : ", ") + modulation_name(e);
    throw UsageError("option '--mod': " + mod + " is not enabled in this build (it supports " + names +
                     "); use --mod " + modulation_name(enabled.front()));
  }

  o.samples_per_symbol = static_cast<int>(v.integer("sps"));
  o.rolloff = v.number("rolloff");
  o.span_symbols = static_cast<int>(v.integer("span"));
  if ((o.samples_per_symbol * o.span_symbols) % 2 != 0) {
    throw UsageError("options '--sps' and '--span': samples per symbol times span must be even, got " +
                     std::to_string(o.samples_per_symbol) + " x " + std::to_string(o.span_symbols) +
                     " (the filter then has a centre tap)");
  }
  o.payload_bytes = static_cast<std::size_t>(v.integer("payload-bytes"));
  o.prbs_seed = static_cast<std::uint32_t>(v.integer("seed"));
  o.sample_rate_hz = v.number("rate");
  return o;
}

void read_receiver_options(const ParsedOptions& v, PhyOptions& phy) {
  phy.detect_threshold = v.number("detect-threshold");
}

SimulationOptions read_simulation_options(const ParsedOptions& v, const PhyOptions& phy) {
  SimulationOptions s;
  sim::ChannelConfig& c = s.channel;
  c.sample_rate_hz = phy.sample_rate_hz;
  c.samples_per_symbol = phy.samples_per_symbol;
  c.cfo_hz = v.number("cfo-hz");
  c.phase_rad = v.number("phase-deg") * kPi / 180.0;
  c.delay_samples = v.number("delay-samples") + v.number("timing-frac");
  c.clock_offset_ppm = v.number("clock-ppm");
  c.gain_db = v.number("amp-db");
  c.freq_drift_hz_per_s = v.number("drift");
  c.phase_walk_rad_per_sqrt_s = v.number("phase-walk");
  c.seed = static_cast<std::uint64_t>(v.integer("noise-seed"));
  if (v.has_value("ebn0")) s.ebn0_db = v.number("ebn0");
  return s;
}

}  // namespace mpsk::app
