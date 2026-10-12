#include "app/cmd_loopback.hpp"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <ostream>
#include <sstream>

#include "app/common_options.hpp"
#include "app/option_parser.hpp"
#include "io/sample_file.hpp"
#include "sim/loopback.hpp"
#include "sim/sweep.hpp"
#include "sim/theory.hpp"

namespace mpsk::app {

namespace {

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
std::string format(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return buf;
}

OptionParser make_parser() {
  OptionParser p("mpsk loopback",
                 "Runs frames through the transmitter, a simulated channel and the receiver, with no hardware.\n"
                 "One run reports every frame and a summary. With --sweep it measures bit error rate against\n"
                 "Eb/N0 and compares it with theory.");
  add_phy_options(p);
  add_receiver_options(p);
  add_simulation_options(p);
  p.group("Run control");
  p.add_integer("frames", "N", "Frames per run; with --sweep, frames per record (at most 5000)", 50, 1, 65535);
  p.add_string("sweep", "START:STOP:STEP", "Sweep Eb/N0 in dB, for example 0:12:2; replaces --ebn0");
  p.add_integer("target-errors", "N", "Sweep: stop a point after this many bit errors", 100, 1, 1000000000LL);
  p.add_integer("max-bits", "N", "Sweep: or after sending this many payload bits (1e6 is accepted)", 1000000, 1,
                1000000000000LL);
  p.add_flag("quiet", "Single run: print only the summary, not a line per frame");
  p.add_string("tx-out", "FILE", "Single run: write the transmitted samples as cf32");
  p.add_string("rx-out", "FILE", "Single run: write the received (impaired) samples as cf32");
  return p;
}

/** The impairments that are switched on, as one line. */
std::string describe_channel(const sim::ChannelConfig& c, const std::optional<double>& ebn0, int order,
                             bool swept = false) {
  std::ostringstream o;
  bool any = false;
  auto add = [&](const std::string& s) {
    o << (any ? ", " : "") << s;
    any = true;
  };
  if (swept) add("Eb/N0 swept");
  else if (ebn0) add(format("Eb/N0 %.1f dB (Es/N0 %.1f dB)", *ebn0, sim::ebn0_to_esn0_db(order, *ebn0)));
  else add("no noise");
  if (c.cfo_hz != 0.0) add(format("carrier offset %.6g Hz", c.cfo_hz));
  if (c.phase_rad != 0.0) add(format("phase %.6g rad", c.phase_rad));
  if (c.delay_samples != 0.0) add(format("delay %.6g samples", c.delay_samples));
  if (c.gain_db != 0.0) add(format("amplitude %+.6g dB", c.gain_db));
  if (c.clock_offset_ppm != 0.0) add(format("clock offset %.6g ppm", c.clock_offset_ppm));
  if (c.freq_drift_hz_per_s != 0.0) add(format("drift %.6g Hz/s", c.freq_drift_hz_per_s));
  if (c.phase_walk_rad_per_sqrt_s != 0.0) add(format("phase walk %.6g rad/sqrt(s)", c.phase_walk_rad_per_sqrt_s));
  return o.str();
}

std::string describe_link(const PhyOptions& phy) {
  std::string mod = modulation_name(phy.modulation_order);
  for (char& c : mod) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return format("%s, %d samples/symbol, %.6g MS/s (%.6g ksym/s), roll-off %.3g, span %d symbols, %zu-byte payloads",
                mod.c_str(), phy.samples_per_symbol, phy.sample_rate_hz / 1e6, phy.symbol_rate_hz() / 1e3,
                phy.rolloff, phy.span_symbols, phy.payload_bytes);
}

void write_samples(io::Cf32Writer& w, const std::string& path, const std::vector<std::complex<float>>& samples,
                   std::ostream& out, const char* what) {
  w.write(samples);
  w.close();
  out << "wrote " << samples.size() << " " << what << " samples to " << path << " (cf32)\n";
}

int run_single(const ParsedOptions& v, const PhyOptions& phy, const SimulationOptions& sim_opts, std::ostream& out) {
  const std::size_t frames = static_cast<std::size_t>(v.integer("frames"));
  const bool quiet = v.flag("quiet");

  sim::LoopbackConfig lc;
  lc.modulator = phy.modulator();
  lc.receiver = phy.receiver();
  lc.payload_bytes = phy.payload_bytes;
  lc.prbs_seed = phy.prbs_seed;
  const sim::LoopbackLink link(lc);

  sim::ChannelConfig channel = sim_opts.channel;
  if (sim_opts.ebn0_db) channel.esn0_db = sim::ebn0_to_esn0_db(phy.modulation_order, *sim_opts.ebn0_db);

  // Open the output files before the run, so a path that cannot be written is reported at
  // once and not after the simulation has finished.
  std::optional<io::Cf32Writer> tx_file, rx_file;
  if (v.given("tx-out")) tx_file.emplace(v.str("tx-out"));
  if (v.given("rx-out")) rx_file.emplace(v.str("rx-out"));
  const bool keep = tx_file || rx_file;
  out << "mpsk loopback: " << describe_link(phy) << "\n";
  out << "channel: " << describe_channel(channel, sim_opts.ebn0_db, phy.modulation_order) << "\n";
  out << "sending " << frames << " frames\n\n";

  const sim::LoopbackResult r = link.run(channel, frames, 1, keep);

  if (!quiet) {
    out << "  frame  counter  header  payload  bit errors   EVM %  EVM SNR dB    CFO Hz  clock ppm\n";
    std::size_t n = 0;
    for (const sim::FrameReport& f : r.frames) {
      ++n;
      if (!f.header_ok) {
        out << format("  %5zu        -  LOST          -           -       -           -         -          -\n", n);
        continue;
      }
      out << format("  %5zu  %7u  ok      %-7s  %10zu  %6.2f  %10.1f  %8.1f  %9.1f\n", n, f.counter,
                    f.payload_crc_ok ? "ok" : "CRC-FAIL", f.bit_errors, f.evm * 100.0, f.evm_snr_db, f.cfo_hz,
                    f.clock_offset_ppm);
    }
    out << "\n";
  }

  double snr = 0.0, cfo = 0.0;
  std::size_t counted = 0;
  for (const sim::FrameReport& f : r.frames) {
    if (!f.delivered) continue;
    snr += f.evm_snr_db;
    cfo += f.cfo_hz;
    ++counted;
  }

  out << "summary\n";
  out << format("  frames sent %zu, detected %zu, delivered %zu, payload CRC ok %zu, lost %zu\n", r.frames_sent,
                r.frames_detected, r.frames_delivered, r.frames_payload_ok, r.frames_lost);
  out << format("  payload bits compared %zu, bit errors %zu\n", r.bits_compared, r.bit_errors);
  if (r.bits_compared == 0) {
    out << "  BER not measured: no frame was delivered\n";
  } else if (r.bit_errors == 0) {
    out << format("  BER 0 (no errors in %zu bits; below %.2g at 95%% confidence)\n", r.bits_compared,
                  3.0 / static_cast<double>(r.bits_compared));
  } else {
    out << format("  BER %.3e\n", r.ber());
  }
  if (sim_opts.ebn0_db) {
    const double theory = sim::mpsk_ber_theory(phy.modulation_order, *sim_opts.ebn0_db);
    out << format("  theory BER at this Eb/N0 %.3e", theory);
    if (r.bit_errors > 0 && r.ber() < 0.5) {
      out << format(", measured needs %+.2f dB more Eb/N0 than theory",
                    *sim_opts.ebn0_db - sim::mpsk_ebn0_for_ber(phy.modulation_order, r.ber()));
    }
    out << "\n";
  }
  if (counted > 0) {
    out << format("  mean EVM SNR %.1f dB, mean carrier offset %.1f Hz (over delivered frames)\n",
                  snr / static_cast<double>(counted), cfo / static_cast<double>(counted));
  }

  if (tx_file) write_samples(*tx_file, v.str("tx-out"), r.transmitted, out, "transmitted");
  if (rx_file) write_samples(*rx_file, v.str("rx-out"), r.received, out, "received");
  return 0;
}

int run_sweep_mode(const ParsedOptions& v, const PhyOptions& phy, const SimulationOptions& sim_opts, std::ostream& out) {
  const std::vector<double> range = parse_colon_triple("--sweep", v.str("sweep"));
  const double start = range[0], stop = range[1], step = range[2];
  if (!(step > 0.0)) throw UsageError("option '--sweep': the step must be positive, got " + format("%g", step));
  if (stop < start) throw UsageError("option '--sweep': STOP must not be below START, got " + v.str("sweep"));
  const double count = std::floor((stop - start) / step + 1e-9) + 1.0;
  if (count > 200.0) {
    throw UsageError("option '--sweep': " + v.str("sweep") + " has " + format("%.0f", count) +
                     " points; at most 200 are allowed");
  }
  const long long frames = v.integer("frames");
  if (frames > 5000) throw UsageError("option '--frames': a sweep takes at most 5000 frames per record, got " + std::to_string(frames));

  sim::SweepConfig sc;
  sc.link.modulator = phy.modulator();
  sc.link.receiver = phy.receiver();
  sc.link.payload_bytes = phy.payload_bytes;
  sc.link.prbs_seed = phy.prbs_seed;
  sc.channel = sim_opts.channel;
  sc.ebn0_db.clear();   // the config's own default list is not wanted
  for (std::size_t k = 0; k < static_cast<std::size_t>(count); ++k) sc.ebn0_db.push_back(start + static_cast<double>(k) * step);
  sc.target_errors = static_cast<std::size_t>(v.integer("target-errors"));
  sc.max_bits = static_cast<std::size_t>(v.integer("max-bits"));
  sc.frames_per_run = static_cast<std::size_t>(frames);
  sc.seed = sim_opts.channel.seed;

  out << "mpsk loopback sweep: " << describe_link(phy) << "\n";
  out << "channel: " << describe_channel(sim_opts.channel, std::nullopt, phy.modulation_order, true) << "\n";
  out << format("sweeping Eb/N0 %g to %g dB in steps of %g; each point stops at %zu bit errors or %zu payload bits\n\n",
                start, stop, step, sc.target_errors, sc.max_bits);
  out << "  Eb/N0 dB   BER measured   errors / bits compared      theory BER   loss dB  frames delivered\n";

  const int order = phy.modulation_order;
  sc.on_point = [&out, order](const sim::SweepPoint& p) {
    const auto loss = p.loss_db(order);
    const std::string l = loss ? format("%+8.2f", *loss) : std::string("     n/a");
    out << format("  %8.1f  %13.3e  %10zu / %-12zu  %14.3e  %s  %zu of %zu%s\n", p.ebn0_db, p.ber(), p.bit_errors,
                  p.bits_compared, p.theory_ber, l.c_str(), p.frames_delivered, p.frames_sent,
                  p.reached_target ? "" : "  (bit cap reached first)");
    out.flush();
  };
  sim::run_sweep(sc);

  out << "\nloss is the extra Eb/N0 the receiver needed over ideal theory to reach the measured BER; it is\n"
         "meaningful when a point reached its error target, and n/a when it saw no errors. The BER is over\n"
         "delivered frames: at low Eb/N0 frames whose header is lost contribute no bits.\n";
  return 0;
}

}  // namespace

int run_loopback(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  (void)err;
  const OptionParser parser = make_parser();
  const ParseOutcome parsed = parser.parse(args);
  if (parsed.help_requested) {
    out << parser.usage();
    return 0;
  }
  const ParsedOptions& v = parsed.values;

  PhyOptions phy = read_phy_options(v);
  read_receiver_options(v, phy);
  const SimulationOptions sim_opts = read_simulation_options(v, phy);

  const bool sweep = v.given("sweep");
  if (sweep && v.given("ebn0")) throw UsageError("give either '--ebn0' for one run or '--sweep' for a range, not both");
  if (sweep && (v.given("tx-out") || v.given("rx-out"))) {
    throw UsageError("'--tx-out' and '--rx-out' write one run's samples; they cannot be used with '--sweep'");
  }
  if (sweep && v.flag("quiet")) throw UsageError("'--quiet' applies to a single run, not to '--sweep'");
  if (!sweep) {
    for (const char* name : {"target-errors", "max-bits"}) {
      if (v.given(name)) throw UsageError(std::string("option '--") + name + "' applies only with '--sweep'");
    }
  }
  return sweep ? run_sweep_mode(v, phy, sim_opts, out) : run_single(v, phy, sim_opts, out);
}

}  // namespace mpsk::app
