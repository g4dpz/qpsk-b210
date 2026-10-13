// Tests for the `mpsk` command line, run in-process: the real dispatcher and the real
// loopback, with the output captured. Task 9; requirements 1.1, 1.2, 1.8 (modulation
// choice, default, 8-PSK refused in stage 1), 6.1 (loopback mode), 10.4 (builds and
// runs without UHD).

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/cli.hpp"
#include "app/option_parser.hpp"
#include "dsp/spectrum.hpp"
#include "io/sample_file.hpp"
#include "temp_file.hpp"
#include "test_harness.hpp"

using namespace mpsk::app;
using Args = std::vector<std::string>;

namespace {

struct Run {
  int code = -1;
  std::string out;
  std::string err;
};

Run cli(const Args& args) {
  std::ostringstream out, err;
  Run r;
  r.code = run_cli(args, out, err);
  r.out = out.str();
  r.err = err.str();
  return r;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

/** The number after @p label on the line that has it, or -1. For "bit errors 553" style output. */
long number_after(const std::string& text, const std::string& label) {
  const std::size_t at = text.find(label);
  if (at == std::string::npos) return -1;
  return std::strtol(text.c_str() + at + label.size(), nullptr, 10);
}

bool built_with_uhd() { return contains(cli({"version"}).out, "UHD radio layer: enabled"); }

}  // namespace

// ---- The command and its help ----------------------------------------------

TEST(no_command_is_a_usage_error_that_shows_the_commands) {
  const Run r = cli({});
  CHECK(r.code == 2);
  CHECK(contains(r.err, "no command given"));
  CHECK(contains(r.err, "loopback"));
  CHECK(r.out.empty());
}

TEST(help_lists_every_command_and_exits_zero) {
  for (const Args& a : {Args{"--help"}, Args{"-h"}, Args{"help"}}) {
    const Run r = cli(a);
    CHECK(r.code == 0);
    for (const char* c : {"loopback", "echo-sim", "tx", "rx", "echo-init", "echo-resp", "version"}) {
      CHECK(contains(r.out, c));
    }
    CHECK(r.err.empty());
  }
}

TEST(an_unknown_command_is_named_with_a_suggestion) {
  const Run r = cli({"loopbak"});
  CHECK(r.code == 2);
  CHECK(contains(r.err, "unknown command 'loopbak'"));
  CHECK(contains(r.err, "did you mean 'loopback'"));
  const Run far = cli({"zzzzzzzz"});
  CHECK(far.code == 2);
  CHECK(!contains(far.err, "did you mean"));
}

TEST(help_for_a_command_is_the_same_by_either_route) {
  const Run a = cli({"loopback", "--help"});
  const Run b = cli({"help", "loopback"});
  CHECK(a.code == 0);
  CHECK(b.code == 0);
  CHECK(a.out == b.out);
  CHECK(contains(a.out, "Usage: mpsk loopback"));
  // Every option the design names for the offline modes is documented.
  for (const char* opt : {"--mod", "--sps", "--rolloff", "--span", "--payload-bytes", "--seed", "--detect-threshold",
                          "--ebn0", "--cfo-hz", "--phase-deg", "--timing-frac", "--delay-samples", "--clock-ppm",
                          "--amp-db", "--drift", "--sweep", "--frames", "--tx-out", "--rx-out", "--profile", "--baud"}) {
    CHECK(contains(a.out, opt));
  }
}

TEST(the_version_command_reports_the_uhd_state) {
  const Run r = cli({"version"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "mpsk "));
  CHECK(contains(r.out, "UHD radio layer:"));
  const Run flag = cli({"--version"});
  CHECK(flag.code == 0);
  CHECK(flag.out == r.out);
  CHECK(cli({"version", "--help"}).code == 0);
  CHECK(cli({"version", "extra"}).code == 2);
}

// ---- Commands that are not available yet -----------------------------------

TEST(hardware_commands_say_why_they_cannot_run_and_exit_nonzero) {
  const bool uhd = built_with_uhd();
  for (const char* name : {"tx", "rx", "echo-init", "echo-resp"}) {
    const Run r = cli({name, "--freq", "2.4e9"});
    CHECK(r.code == 1);
    CHECK(r.out.empty());
    if (uhd) {
      CHECK(contains(r.err, "not available yet"));
    } else {
      CHECK(contains(r.err, "built without UHD"));
      CHECK(contains(r.err, "-DMPSK_WITH_UHD=ON"));
    }
    CHECK(contains(r.err, name));
  }
}

TEST(echo_sim_is_declared_and_says_it_is_not_written_yet) {
  const Run r = cli({"echo-sim"});
  CHECK(r.code == 1);
  CHECK(contains(r.err, "not available yet"));
  CHECK(contains(r.err, "task 11"));
  CHECK(!contains(r.err, "built without UHD"));   // it needs no radio
}

TEST(a_command_that_is_not_available_still_answers_help) {
  for (const char* name : {"tx", "rx", "echo-init", "echo-resp", "echo-sim"}) {
    const Run r = cli({name, "--help"});
    CHECK(r.code == 0);
    CHECK(contains(r.out, std::string("Usage: mpsk ") + name));
    CHECK(contains(r.out, "Not available yet"));
    const Run h = cli({"help", name});
    CHECK(h.code == 0);
    CHECK(h.out == r.out);
  }
}

// ---- Loopback: a clean run --------------------------------------------------

TEST(a_noiseless_loopback_reports_no_errors_in_more_than_10000_bits) {
  const Run r = cli({"loopback", "--frames", "20"});
  CHECK(r.code == 0);
  CHECK(r.err.empty());
  CHECK(contains(r.out, "QPSK"));                                  // the default (requirement 1.2)
  CHECK(contains(r.out, "channel: no noise"));
  CHECK(number_after(r.out, "frames sent ") == 20);
  CHECK(number_after(r.out, "delivered ") == 20);
  CHECK(number_after(r.out, "payload CRC ok ") == 20);
  CHECK(number_after(r.out, "lost ") == 0);
  CHECK(number_after(r.out, "payload bits compared ") == 20480);
  CHECK(number_after(r.out, "bit errors ") == 0);
  CHECK(contains(r.out, "BER 0 (no errors in 20480 bits"));
}

TEST(each_frame_gets_a_line_unless_quiet) {
  const Run loud = cli({"loopback", "--frames", "5"});
  CHECK(contains(loud.out, "frame  counter  header  payload"));
  int lines = 0;
  std::istringstream in(loud.out);
  for (std::string line; std::getline(in, line);) {
    if (line.size() > 20 && line.find("ok      ok") != std::string::npos) ++lines;
  }
  CHECK(lines == 5);

  const Run quiet = cli({"loopback", "--frames", "5", "--quiet"});
  CHECK(quiet.code == 0);
  CHECK(!contains(quiet.out, "frame  counter"));
  CHECK(contains(quiet.out, "summary"));
  CHECK(number_after(quiet.out, "delivered ") == 5);
}

TEST(impairments_are_applied_and_described) {
  const Run r = cli({"loopback", "--frames", "6", "--quiet", "--cfo-hz", "1500", "--delay-samples", "700.5",
                      "--timing-frac", "0.25", "--amp-db", "-30", "--phase-deg", "40", "--clock-ppm", "20",
                      "--drift", "100", "--phase-walk", "1", "--ebn0", "20"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "Eb/N0 20.0 dB (Es/N0 23.0 dB)"));
  CHECK(contains(r.out, "carrier offset 1500 Hz"));
  CHECK(contains(r.out, "delay 700.75 samples"));      // the whole and the fractional part add
  CHECK(contains(r.out, "amplitude -30 dB"));
  CHECK(contains(r.out, "clock offset 20 ppm"));
  CHECK(contains(r.out, "drift 100 Hz/s"));
  CHECK(contains(r.out, "phase walk 1 rad/sqrt(s)"));
  CHECK(number_after(r.out, "delivered ") == 6);
  CHECK(number_after(r.out, "bit errors ") == 0);
  // And the receiver measured the offset that was applied.
  const std::size_t at = r.out.find("mean carrier offset ");
  REQUIRE(at != std::string::npos);
  CHECK_NEAR(std::strtod(r.out.c_str() + at + 20, nullptr), 1500.0, 30.0);   // plus a little drift
}

TEST(a_negative_value_is_taken_as_a_value) {
  const Run r = cli({"loopback", "--frames", "3", "--quiet", "--cfo-hz", "-2500"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "carrier offset -2500 Hz"));
  CHECK(number_after(r.out, "delivered ") == 3);
}

TEST(noise_gives_errors_a_theory_comparison_and_a_loss_figure) {
  const Run r = cli({"loopback", "--frames", "40", "--quiet", "--ebn0", "4"});
  CHECK(r.code == 0);
  CHECK(number_after(r.out, "bit errors ") > 100);
  CHECK(contains(r.out, "theory BER at this Eb/N0 1.250e-02"));
  CHECK(contains(r.out, "than theory"));
  CHECK(!contains(r.out, "BER 0 "));
}

TEST(the_same_options_give_the_same_report_and_another_noise_seed_another) {
  const Args base = {"loopback", "--frames", "20", "--quiet", "--ebn0", "3"};
  const Run a = cli(base);
  const Run b = cli(base);
  CHECK(a.out == b.out);
  Args other = base;
  other.push_back("--noise-seed");
  other.push_back("7");
  CHECK(cli(other).out != a.out);
}

TEST(pulse_and_payload_options_reach_the_link) {
  const Run r = cli({"loopback", "--frames", "4", "--quiet", "--sps", "8", "--rolloff", "0.5", "--span", "9",
                      "--payload-bytes", "40", "--seed", "99", "--rate", "2e6"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "8 samples/symbol"));
  CHECK(contains(r.out, "roll-off 0.5"));
  CHECK(contains(r.out, "span 9 symbols"));
  CHECK(contains(r.out, "40-byte payloads"));
  CHECK(contains(r.out, "sample rate 2 MS/s, symbol rate 250000 Bd"));
  CHECK(number_after(r.out, "payload bits compared ") == 4 * 40 * 8);
  CHECK(number_after(r.out, "bit errors ") == 0);
}

TEST(the_detection_threshold_option_is_used) {
  // At 7 kHz of carrier offset the stage-1 statistic peaks near 0.49 (sinc^2 of 16 f / Rs), so
  // a threshold of 0.25 finds the frames and a threshold of 0.6 cannot.
  const Run found = cli({"loopback", "--frames", "3", "--quiet", "--cfo-hz", "7000"});
  CHECK(number_after(found.out, "detected ") == 3);
  CHECK(number_after(found.out, "delivered ") == 3);
  const Run missed = cli({"loopback", "--frames", "3", "--quiet", "--cfo-hz", "7000", "--detect-threshold", "0.6"});
  CHECK(missed.code == 0);
  CHECK(number_after(missed.out, "detected ") == 0);
  CHECK(number_after(missed.out, "delivered ") == 0);
  CHECK(number_after(missed.out, "lost ") == 3);
  CHECK(contains(missed.out, "BER not measured"));
}

// ---- Loopback: files --------------------------------------------------------

TEST(sample_files_are_written_as_cf32_with_the_reported_sizes) {
  TempPath tx(".cf32"), rx(".cf32");
  const Run r = cli({"loopback", "--frames", "3", "--quiet", "--delay-samples", "500", "--tx-out", tx.str(),
                      "--rx-out", rx.str()});
  CHECK(r.code == 0);
  REQUIRE(tx.size() > 0);
  REQUIRE(rx.size() > 0);
  CHECK(tx.size() % 8 == 0);
  CHECK(rx.size() % 8 == 0);
  CHECK(rx.size() >= tx.size() + 500 * 8);                    // the delay lengthens the received record
  CHECK(contains(r.out, "wrote " + std::to_string(tx.size() / 8) + " transmitted samples"));
  CHECK(contains(r.out, "wrote " + std::to_string(rx.size() / 8) + " received samples"));

  // The transmitted file holds the burst: silence, then a signal of the modulator's size.
  mpsk::io::Cf32Reader reader(tx.str());
  const auto samples = reader.read(100000);
  REQUIRE(samples.size() > 1000);
  double peak = 0;
  for (const auto& s : samples) peak = std::max<double>(peak, std::abs(s));
  CHECK(peak > 0.3 && peak < 0.6);   // output scale 0.5, peak 0.40 of full scale per the modulator
}

TEST(only_the_file_asked_for_is_written) {
  TempPath tx(".cf32"), rx(".cf32");
  CHECK(cli({"loopback", "--frames", "2", "--quiet", "--tx-out", tx.str()}).code == 0);
  CHECK(tx.size() > 0);
  CHECK(rx.size() == -1);
}

TEST(an_unwritable_output_path_fails_at_once_with_status_1) {
  const Run r = cli({"loopback", "--frames", "2", "--tx-out", "/nonexistent-dir-for-mpsk-test/x.cf32"});
  CHECK(r.code == 1);
  CHECK(contains(r.err, "cannot open"));
  CHECK(contains(r.err, "/nonexistent-dir-for-mpsk-test/x.cf32"));
  CHECK(!contains(r.out, "summary"));                        // it did not run first
}

// ---- Loopback: the sweep ----------------------------------------------------

TEST(a_sweep_prints_one_row_per_requested_point_and_no_others) {
  const Run r = cli({"loopback", "--sweep", "0:4:2", "--max-bits", "60000", "--target-errors", "50"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "Eb/N0 swept"));
  std::vector<double> points;
  std::istringstream in(r.out);
  for (std::string line; std::getline(in, line);) {
    // Rows start with the Eb/N0 value and carry the "x of y" delivered column.
    if (line.find(" of ") == std::string::npos || line.find('/') == std::string::npos) continue;
    if (line.rfind("  ", 0) != 0) continue;
    char* end = nullptr;
    const double v = std::strtod(line.c_str(), &end);
    if (end != line.c_str()) points.push_back(v);
  }
  REQUIRE(points.size() == 3);
  CHECK_NEAR(points[0], 0.0, 1e-9);
  CHECK_NEAR(points[1], 2.0, 1e-9);
  CHECK_NEAR(points[2], 4.0, 1e-9);
  CHECK(contains(r.out, "theory BER"));
  CHECK(contains(r.out, "loss is the extra Eb/N0"));
}

TEST(a_sweep_step_that_does_not_divide_the_range_stops_inside_it) {
  const Run r = cli({"loopback", "--sweep", "1:4:1.5", "--max-bits", "30000", "--target-errors", "30"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "1.0 "));
  CHECK(contains(r.out, "2.5 "));
  CHECK(contains(r.out, "4.0 "));
  CHECK(!contains(r.out, "5.5 "));
}

TEST(a_sweep_point_that_reaches_the_bit_cap_first_says_so) {
  const Run r = cli({"loopback", "--sweep", "12:12:1", "--max-bits", "20000"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "(bit cap reached first)"));
  CHECK(contains(r.out, "n/a"));
}

TEST(a_sweep_gives_the_same_numbers_as_the_library_for_the_same_settings) {
  // 0 dB, 100 errors or 200 000 bits, 50 frames per record, noise seed 1: the sweep library
  // measured 906 errors in 10 240 bits, 10 of 50 frames delivered, for these settings (the
  // first run already passes 100 errors, so the point is one run).
  const Run r = cli({"loopback", "--sweep", "0:0:1", "--max-bits", "200000"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "906 / 10240"));
  CHECK(contains(r.out, "10 of 50"));
}

// ---- Loopback: refusals -----------------------------------------------------

TEST(eight_psk_is_refused_in_stage_1_with_a_clear_message) {
  const Run r = cli({"loopback", "--mod", "8psk"});
  CHECK(r.code == 2);
  CHECK(contains(r.err, "option '--mod'"));
  CHECK(contains(r.err, "8psk is not enabled"));
  CHECK(contains(r.err, "qpsk"));
  CHECK(r.out.empty());                                      // nothing ran
}

TEST(qpsk_can_be_named_and_is_case_insensitive) {
  const Run r = cli({"loopback", "--mod", "QPSK", "--frames", "2", "--quiet"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "QPSK"));
}

TEST(a_bad_value_exits_2_naming_the_option_and_pointing_to_help) {
  struct Case { Args args; std::string expect; };
  const std::vector<Case> cases = {
      {{"--sps", "abc"}, "option '--sps': 'abc' is not a number"},
      {{"--sps", "4.5"}, "option '--sps': '4.5' is not a whole number"},
      {{"--sps", "1"}, "option '--sps': 1 is below the minimum of 2"},
      {{"--cfo-hz"}, "option '--cfo-hz' needs a value"},
      {{"--frames", "0"}, "option '--frames': 0 is below the minimum of 1"},
      {{"--rolloff", "0.05"}, "option '--rolloff': 0.05 is below the minimum of 0.1"},
      {{"--payload-bytes", "9999"}, "option '--payload-bytes': 9999 is above the maximum of 4096"},
      {{"--mod", "bpsk"}, "option '--mod': 'bpsk' is not one of: qpsk, 8psk"},
      {{"--ebn0", "nan"}, "option '--ebn0'"},
      {{"--frames", "3", "--frames", "4"}, "option '--frames' was given twice"},
      {{"--foo", "1"}, "unknown option '--foo'"},
      {{"--cfo", "5"}, "unknown option '--cfo'"},
      {{"stray"}, "unexpected argument 'stray'"},
  };
  for (const Case& c : cases) {
    Args a = {"loopback"};
    a.insert(a.end(), c.args.begin(), c.args.end());
    const Run r = cli(a);
    if (r.code != 2 || !contains(r.err, c.expect)) {
      std::fprintf(stderr, "    case: %s -> exit %d, stderr: %s\n", c.args[0].c_str(), r.code, r.err.c_str());
    }
    CHECK(r.code == 2);
    CHECK(contains(r.err, "mpsk loopback: error:"));
    CHECK(contains(r.err, c.expect));
    CHECK(contains(r.err, "Try 'mpsk loopback --help'"));
    CHECK(r.out.empty());
  }
}

TEST(a_misspelt_option_suggests_the_right_one) {
  const Run r = cli({"loopback", "--rolof", "0.3"});
  CHECK(r.code == 2);
  CHECK(contains(r.err, "did you mean '--rolloff'"));
}

TEST(options_that_conflict_are_refused_naming_both) {
  struct Case { Args args; std::string expect; };
  const std::vector<Case> cases = {
      {{"--ebn0", "3", "--sweep", "0:2:1"}, "either '--ebn0'"},
      {{"--sweep", "0:2:1", "--tx-out", "x"}, "'--tx-out' and '--rx-out'"},
      {{"--sweep", "0:2:1", "--quiet"}, "'--quiet' applies to a single run"},
      {{"--target-errors", "5"}, "'--target-errors' applies only with '--sweep'"},
      {{"--max-bits", "5"}, "'--max-bits' applies only with '--sweep'"},
      {{"--sweep", "0:2"}, "START:STOP:STEP"},
      {{"--sweep", "4:2:1"}, "STOP must not be below START"},
      {{"--sweep", "0:2:0"}, "the step must be positive"},
      {{"--sweep", "0:2:-1"}, "the step must be positive"},
      {{"--sweep", "0:1000:1"}, "at most 200"},
      {{"--sweep", "0:2:1", "--frames", "6000"}, "at most 5000 frames per record"},
      {{"--sps", "5", "--span", "11"}, "options '--sps' and '--span': samples per symbol times span must be even"},
  };
  for (const Case& c : cases) {
    Args a = {"loopback"};
    a.insert(a.end(), c.args.begin(), c.args.end());
    const Run r = cli(a);
    CHECK(r.code == 2);
    CHECK(contains(r.err, c.expect));
    CHECK(r.out.empty());
  }
}

// ---- Settings reach the channel and the link -------------------------------

TEST(the_phase_option_is_in_degrees) {
  const Run r = cli({"loopback", "--frames", "2", "--quiet", "--phase-deg", "40"});
  CHECK(contains(r.out, "phase 0.698132 rad"));   // 40 degrees
  CHECK(number_after(r.out, "delivered ") == 2);
}

TEST(the_payload_seed_changes_the_transmitted_samples) {
  TempPath a(".cf32"), b(".cf32"), c(".cf32");
  CHECK(cli({"loopback", "--frames", "1", "--quiet", "--seed", "3", "--tx-out", a.str()}).code == 0);
  CHECK(cli({"loopback", "--frames", "1", "--quiet", "--seed", "3", "--tx-out", b.str()}).code == 0);
  CHECK(cli({"loopback", "--frames", "1", "--quiet", "--seed", "4", "--tx-out", c.str()}).code == 0);
  auto read = [](const std::string& path) { return mpsk::io::Cf32Reader(path).read(1000000); };
  const auto sa = read(a.str()), sb = read(b.str()), sc = read(c.str());
  REQUIRE(sa.size() == sb.size());
  REQUIRE(sa.size() == sc.size());
  CHECK(sa == sb);
  CHECK(sa != sc);
}

TEST(a_clean_run_states_the_bound_it_can_claim_and_a_noisy_run_the_sign_of_its_loss) {
  const Run clean = cli({"loopback", "--frames", "20", "--quiet"});
  CHECK(contains(clean.out, "below 0.00015 at 95% confidence"));   // 3 / 20480
  const Run noisy = cli({"loopback", "--frames", "40", "--quiet", "--ebn0", "4"});
  CHECK(contains(noisy.out, "needs +"));   // the receiver is slightly worse than ideal, never better
}

TEST(the_noise_seed_reaches_the_sweep) {
  const Args base = {"loopback", "--sweep", "2:2:1", "--max-bits", "30000", "--target-errors", "200"};
  Args other = base;
  other.push_back("--noise-seed");
  other.push_back("5");
  const Run a = cli(base), b = cli(other);
  CHECK(a.code == 0 && b.code == 0);
  CHECK(a.out != b.out);
  CHECK(cli(base).out == a.out);
}

// ---- Errors are mapped to exit statuses -------------------------------------

TEST(each_kind_of_failure_gets_its_own_status_and_message) {
  std::ostringstream out, err;
  auto throwing = [](auto make) {
    return CommandHandler([make](const Args&, std::ostream&, std::ostream&) -> int { make(); return 0; });
  };

  err.str("");
  CHECK(run_guarded("demo", throwing([] { throw UsageError("bad thing"); }), {}, out, err) == 2);
  CHECK(contains(err.str(), "mpsk demo: error: bad thing"));
  CHECK(contains(err.str(), "Try 'mpsk demo --help'"));

  err.str("");
  CHECK(run_guarded("demo", throwing([] { throw std::invalid_argument("library says no"); }), {}, out, err) == 2);
  CHECK(contains(err.str(), "mpsk demo: error: library says no"));
  CHECK(contains(err.str(), "Try 'mpsk demo --help'"));

  err.str("");
  CHECK(run_guarded("demo", throwing([] { throw std::logic_error("bug"); }), {}, out, err) == 3);
  CHECK(contains(err.str(), "internal error: bug"));
  CHECK(!contains(err.str(), "Try"));

  err.str("");
  CHECK(run_guarded("demo", throwing([] { throw std::runtime_error("disk full"); }), {}, out, err) == 1);
  CHECK(contains(err.str(), "mpsk demo: error: disk full"));
  CHECK(!contains(err.str(), "Try"));

  err.str("");
  CHECK(run_guarded("demo", throwing([] { throw 42; }), {}, out, err) == 3);
  CHECK(contains(err.str(), "unknown exception"));

  // And a handler that returns normally has its status passed through.
  CHECK(run_guarded("demo", CommandHandler([](const Args&, std::ostream&, std::ostream&) { return 7; }), {}, out, err) == 7);
}

// ---- The sweep honours every setting ---------------------------------------
//
// Each case is a one-point sweep at 12 dB, where every frame of a healthy link is delivered
// and no bit error is expected, so what changes the row is the setting under test.

TEST(a_sweep_uses_the_payload_size_frames_per_record_and_bit_cap_it_is_given) {
  const Run base = cli({"loopback", "--sweep", "12:12:1", "--max-bits", "20000"});
  CHECK(contains(base.out, " 0 / 51200"));            // 50 frames of 128 bytes in one record
  CHECK(contains(base.out, "50 of 50"));

  const Run small = cli({"loopback", "--sweep", "12:12:1", "--max-bits", "20000", "--payload-bytes", "64"});
  CHECK(contains(small.out, " 0 / 25600"));           // 50 frames of 64 bytes
  CHECK(contains(small.out, "50 of 50"));

  const Run frames = cli({"loopback", "--sweep", "12:12:1", "--max-bits", "20000", "--frames", "10"});
  CHECK(contains(frames.out, " 0 / 20480"));          // two records of 10 frames reach the cap
  CHECK(contains(frames.out, "20 of 20"));
  CHECK(contains(frames.out, "(bit cap reached first)"));
}

TEST(a_sweep_stops_on_the_error_target_it_is_given) {
  // One frame per record at 0 dB: the first delivered frame has dozens of errors, so a target
  // of 1 stops after exactly one delivered frame's 1024 bits. The default target of 100 is
  // above what one frame gives and needs more.
  const Run one = cli({"loopback", "--sweep", "0:0:1", "--frames", "1", "--target-errors", "1"});
  CHECK(contains(one.out, " / 1024 "));
  CHECK(!contains(one.out, "(bit cap reached first)"));
  const Run many = cli({"loopback", "--sweep", "0:0:1", "--frames", "1"});
  CHECK(!contains(many.out, " / 1024 "));
}

TEST(a_sweep_applies_the_channel_and_receiver_settings) {
  // 20 kHz of carrier offset is far outside the acquisition range: nothing is delivered.
  const Run out_of_range = cli({"loopback", "--sweep", "12:12:1", "--cfo-hz", "20000", "--max-bits", "30000"});
  CHECK(contains(out_of_range.out, " 0 / 0 "));
  CHECK(contains(out_of_range.out, "  0 of 50"));

  // 7 kHz is inside it, unless the detection threshold is raised above what that offset allows.
  const Run found = cli({"loopback", "--sweep", "12:12:1", "--cfo-hz", "7000", "--max-bits", "30000"});
  CHECK(contains(found.out, "  50 of 50"));
  const Run missed = cli({"loopback", "--sweep", "12:12:1", "--cfo-hz", "7000", "--detect-threshold", "0.6",
                          "--max-bits", "30000"});
  CHECK(contains(missed.out, "  0 of 50"));
}

TEST(a_sweep_uses_the_pulse_options_for_transmitter_and_receiver_alike) {
  const Run r = cli({"loopback", "--sweep", "12:12:1", "--sps", "8", "--rolloff", "0.5", "--max-bits", "20000"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "8 samples/symbol"));
  CHECK(contains(r.out, "50 of 50"));
}

TEST(a_sweep_range_includes_its_end_despite_rounding) {
  // 0.3 / 0.1 is 2.9999999999999996 in floating point; the end point must still be a row.
  const Run r = cli({"loopback", "--sweep", "0:0.3:0.1", "--max-bits", "2000", "--target-errors", "1"});
  CHECK(r.code == 0);
  int rows = 0;
  std::istringstream in(r.out);
  for (std::string line; std::getline(in, line);) {
    if (line.rfind("  ", 0) == 0 && line.find(" of ") != std::string::npos && line.find(" / ") != std::string::npos) ++rows;
  }
  CHECK(rows == 4);
  CHECK(contains(r.out, "0.3 "));
}

// ---- The per-frame lines and the means --------------------------------------

TEST(a_frame_whose_header_is_lost_is_marked_and_a_failed_crc_is_shown) {
  const Run lost = cli({"loopback", "--ebn0", "0", "--frames", "10"});
  CHECK(contains(lost.out, "LOST"));
  const Run crc = cli({"loopback", "--ebn0", "4", "--frames", "10"});
  CHECK(contains(crc.out, "CRC-FAIL"));
  const Run clean = cli({"loopback", "--frames", "10"});
  CHECK(!contains(clean.out, "LOST"));
  CHECK(!contains(clean.out, "CRC-FAIL"));
}

TEST(the_evm_column_is_a_percentage) {
  const Run r = cli({"loopback", "--frames", "3"});
  std::istringstream in(r.out);
  int checked = 0;
  for (std::string line; std::getline(in, line);) {
    std::istringstream words(line);
    std::vector<std::string> w;
    for (std::string x; words >> x;) w.push_back(x);
    // "  1  1  ok  ok  0  1.04  39.7  -0.1  -0.1": index, counter, header, payload, errors, EVM %, ...
    if (w.size() == 9 && w[2] == "ok" && w[3] == "ok") {
      const double evm_percent = std::strtod(w[5].c_str(), nullptr);
      CHECK(evm_percent > 0.5 && evm_percent < 2.0);       // a clean frame: about 1%
      const double snr = std::strtod(w[6].c_str(), nullptr);
      CHECK_NEAR(snr, -20.0 * std::log10(evm_percent / 100.0), 0.2);   // and the SNR agrees with it
      ++checked;
    }
  }
  CHECK(checked == 3);
}

TEST(the_means_cover_only_the_frames_that_were_delivered) {
  // At 0 dB most frames are lost; their zero entries must not drag the mean toward 0. The
  // delivered ones have an EVM SNR near the Es/N0 of 3 dB.
  const Run r = cli({"loopback", "--ebn0", "0", "--frames", "100", "--quiet"});
  const std::size_t at = r.out.find("mean EVM SNR ");
  REQUIRE(at != std::string::npos);
  const double snr = std::strtod(r.out.c_str() + at + 13, nullptr);
  CHECK(snr > 2.5 && snr < 5.5);
}

TEST(the_sample_rate_sets_how_far_a_carrier_offset_can_go) {
  // The acquisition range is a fraction of the symbol rate, so 12 kHz is out of range at 1 MS/s
  // (250 ksym/s, limit 7.5 kHz) and inside it at 2 MS/s (500 ksym/s, limit 15 kHz).
  const Run slow = cli({"loopback", "--frames", "3", "--quiet", "--cfo-hz", "12000"});
  CHECK(number_after(slow.out, "delivered ") == 0);
  const Run fast = cli({"loopback", "--frames", "3", "--quiet", "--cfo-hz", "12000", "--rate", "2e6"});
  CHECK(number_after(fast.out, "delivered ") == 3);
}

// ---- The narrowband profile and --baud (task 10A.1) -------------------------

TEST(the_narrowband_profile_sets_2000_baud_roll_off_0_25_and_span_21) {
  const Run r = cli({"loopback", "--profile", "qo100-nb", "--frames", "10", "--quiet"});
  CHECK(r.code == 0);
  CHECK(contains(r.out, "symbol rate 2000 Bd"));
  CHECK(contains(r.out, "sample rate 8 kS/s"));
  CHECK(contains(r.out, "roll-off 0.25"));
  CHECK(contains(r.out, "span 21 symbols"));
  CHECK(contains(r.out, "4 samples/symbol"));
  CHECK(contains(r.out, "QPSK"));
  CHECK(contains(r.out, "(profile qo100-nb)"));
  CHECK(number_after(r.out, "delivered ") == 10);
  CHECK(number_after(r.out, "bit errors ") == 0);
  // The default is not described as a profile.
  CHECK(!contains(cli({"loopback", "--frames", "2", "--quiet"}).out, "(profile"));
}

TEST(an_option_given_with_the_profile_overrides_the_profile) {
  auto header = [](const Args& extra) {
    Args a = {"loopback", "--profile", "qo100-nb", "--frames", "2", "--quiet"};
    a.insert(a.end(), extra.begin(), extra.end());
    return cli(a);
  };
  const Run roll = header({"--rolloff", "0.35"});
  CHECK(roll.code == 0);
  CHECK(contains(roll.out, "roll-off 0.35"));
  CHECK(contains(roll.out, "span 21 symbols"));            // the rest of the profile stays
  CHECK(contains(roll.out, "symbol rate 2000 Bd"));

  const Run span = header({"--span", "11"});
  CHECK(contains(span.out, "span 11 symbols"));
  CHECK(contains(span.out, "roll-off 0.25"));

  const Run baud = header({"--baud", "1800"});
  CHECK(contains(baud.out, "symbol rate 1800 Bd"));
  CHECK(contains(baud.out, "sample rate 7.2 kS/s"));

  const Run sps = header({"--sps", "8"});
  CHECK(contains(sps.out, "symbol rate 2000 Bd"));         // baud holds, the sample rate follows
  CHECK(contains(sps.out, "sample rate 16 kS/s"));
  CHECK(contains(sps.out, "8 samples/symbol"));

  const Run rate = header({"--rate", "16000"});
  CHECK(contains(rate.out, "sample rate 16 kS/s"));
  CHECK(contains(rate.out, "symbol rate 4000 Bd"));        // an explicit sample rate beats the profile's baud
}

TEST(baud_sets_the_sample_rate_to_baud_times_samples_per_symbol) {
  const Run r = cli({"loopback", "--baud", "2000", "--frames", "2", "--quiet"});
  CHECK(contains(r.out, "sample rate 8 kS/s, symbol rate 2000 Bd"));
  CHECK(contains(r.out, "roll-off 0.35"));                 // the default profile's pulse
  CHECK(contains(r.out, "span 11 symbols"));
  const Run s = cli({"loopback", "--baud", "1500", "--sps", "6", "--frames", "2", "--quiet"});
  CHECK(contains(s.out, "sample rate 9 kS/s, symbol rate 1500 Bd"));
  // 250 kBd is the default setting, and gives the same run.
  const Run def = cli({"loopback", "--frames", "5", "--quiet"});
  CHECK(cli({"loopback", "--baud", "250000", "--frames", "5", "--quiet"}).out == def.out);
}

TEST(baud_and_rate_together_are_refused_and_so_is_a_rate_above_the_limit) {
  const Run both = cli({"loopback", "--baud", "2000", "--rate", "8000"});
  CHECK(both.code == 2);
  CHECK(contains(both.err, "either '--rate' (sample rate) or '--baud' (symbol rate), not both"));
  CHECK(cli({"loopback", "--profile", "qo100-nb", "--baud", "2000", "--rate", "8000"}).code == 2);
  const Run big = cli({"loopback", "--baud", "5e7", "--sps", "64"});
  CHECK(big.code == 2);
  CHECK(contains(big.err, "above 1e9"));
  CHECK(cli({"loopback", "--profile", "wideband"}).code == 2);
  CHECK(contains(cli({"loopback", "--profile", "wideband"}).err, "option '--profile': 'wideband' is not one of: default, qo100-nb"));
}

TEST(a_carrier_offset_beyond_the_preamble_range_is_noted) {
  // 3% of the symbol rate: 7.5 kHz at 250 kBd, 60 Hz at 2000 Bd.
  const Run in_range = cli({"loopback", "--frames", "2", "--quiet", "--cfo-hz", "7000"});
  CHECK(!contains(in_range.out, "note:"));
  const Run beyond = cli({"loopback", "--frames", "2", "--quiet", "--cfo-hz", "-8000"});
  CHECK(contains(beyond.out, "note: a carrier offset of -8000 Hz is beyond the reliable acquisition range of about +-7500 Hz"));

  const Run nb_ok = cli({"loopback", "--profile", "qo100-nb", "--frames", "2", "--quiet", "--cfo-hz", "50"});
  CHECK(!contains(nb_ok.out, "note:"));
  CHECK(number_after(nb_ok.out, "delivered ") == 2);
  const Run nb = cli({"loopback", "--profile", "qo100-nb", "--frames", "2", "--quiet", "--cfo-hz", "300"});
  CHECK(contains(nb.out, "beyond the reliable acquisition range of about +-60 Hz"));
  CHECK(number_after(nb.out, "delivered ") == 0);            // and it is true: nothing is found until 10A.4

  const Run sweep = cli({"loopback", "--sweep", "10:10:1", "--max-bits", "5000", "--cfo-hz", "9000"});
  CHECK(contains(sweep.out, "note: a carrier offset of 9000 Hz"));
}

TEST(the_profile_signal_the_command_writes_fits_the_2700_hz_channel) {
  // End to end (Requirement 12.2): the samples the profile actually transmits, read back from
  // the file, measured at the profile's rates.
  TempPath tx(".cf32");
  const Run r = cli({"loopback", "--profile", "qo100-nb", "--frames", "40", "--quiet", "--tx-out", tx.str()});
  CHECK(r.code == 0);
  const auto samples = mpsk::io::Cf32Reader(tx.str()).read(10000000);
  REQUIRE(samples.size() > 40000);
  const auto bw = mpsk::dsp::measure_occupied_bandwidth(samples, 8000.0, 2000.0);
  CHECK(bw.width_26_db_hz <= 2600.0);
  CHECK(bw.width_40_db_hz <= 2700.0);                        // span 21 keeps even the far skirts inside
  CHECK(bw.power_99_hz < 2300.0);
}

TEST(the_profile_link_works_in_noise) {
  const Run r = cli({"loopback", "--profile", "qo100-nb", "--frames", "30", "--quiet", "--ebn0", "9", "--cfo-hz", "40",
                     "--clock-ppm", "10"});
  CHECK(r.code == 0);
  CHECK(number_after(r.out, "delivered ") == 30);
  // The ideal BER at 9 dB is 3.4e-5, about one error in these 30 720 bits; the receiver's loss
  // of 0.1 to 0.2 dB at this SNR makes it one to five. A bound of 30 (BER 1e-3) says the link
  // works without making a test of a Poisson count.
  CHECK(number_after(r.out, "bit errors ") <= 30);
}

MPSK_TEST_MAIN()
