// Statistical tests of the demodulator in noise: many frames each, so they live in their
// own executable and no single CTest entry runs close to the CI timeout in a Debug build.
// Requirements 3.3, 3.4 (carrier, timing), 4.1, 4.2 (EVM, BER), 4.3 (CRC reporting).

#include <cmath>
#include <cstdint>

#include "demod_decode.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using namespace rx_test;

TEST(the_loop_refines_the_preambles_frequency_estimate) {
  // At 10 dB the preamble's frequency is good to about 60 Hz rms. After the carrier
  // loop has run over a frame, the averaged estimate is good to a few hertz.
  double preamble_err = 0, loop_err = 0;
  const int trials = 16;
  for (int i = 0; i < trials; ++i) {
    Scenario s;
    s.esn0_db = 10.0;
    s.cfo_hz = 2000.0;
    s.seed = 700 + static_cast<std::uint64_t>(i);
    s.delay = (i % 10) / 10.0;
    s.phase_rad = 0.3 * i;
    const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
    REQUIRE(d.found && d.result.frame.header_ok);
    const double pe = d.est.cfo_hz(kSymbolRate) - 2000.0;
    const double le = d.result.cfo_hz(kSymbolRate) - 2000.0;
    preamble_err += pe * pe;
    loop_err += le * le;
  }
  CHECK(std::sqrt(loop_err / trials) < 10.0);
  CHECK(std::sqrt(loop_err / trials) < 0.3 * std::sqrt(preamble_err / trials));
}


TEST(ber_at_eight_db_is_close_to_theory) {
  // Gray-coded QPSK at symbol SNR g = Es/N0 has BER = Q(sqrt(g)). At 10 dB that is
  // 7.8e-4; at 8 dB 6.0e-3. 8 dB is used because it yields about 370 errors from 60
  // frames, where 10 dB would need 160 frames (15 s in a Debug build) for 130. Measured
  // at the default loop bandwidths: 6.5e-3 at 8 dB (1.08 times the ideal) and 8.6e-4 at
  // 10 dB from 400 frames (1.10 times). This is a loose check: the sweep against theory
  // is task 8.
  const double g = std::pow(10.0, 0.8);
  const double ideal = 0.5 * std::erfc(std::sqrt(g) / std::sqrt(2.0));
  CHECK_NEAR(ideal, 6.0e-3, 0.1e-3);

  std::size_t errors = 0, bits = 0;
  int lost = 0;
  const int frames = 60;
  for (int i = 0; i < frames; ++i) {
    Scenario s;
    s.esn0_db = 8.0;
    s.cfo_hz = 2000.0;
    s.seed = 4000 + static_cast<std::uint64_t>(i);
    s.delay = (i % 10) / 10.0;
    s.phase_rad = 0.3 * i;
    const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
    if (!d.found || !d.result.frame.header_ok) {
      ++lost;
      continue;
    }
    errors += d.ber.bit_errors;
    bits += d.ber.bits_compared;
  }
  CHECK(lost <= 2);
  CHECK(bits > 55000);
  const double ber = static_cast<double>(errors) / static_cast<double>(bits);
  CHECK(ber > 0.6 * ideal);    // no better than theory, within statistics
  CHECK(ber < 2.0 * ideal);    // and within a factor of two of it
}


TEST(the_evm_snr_tracks_the_true_snr_through_the_whole_receiver) {
  // Measured: 19.9, 14.9, 9.9 and 7.9 dB for true Es/N0 of 20, 15, 10 and 8.
  for (const double esn0 : {20.0, 15.0, 10.0}) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < 8; ++i) {
      Scenario s;
      s.esn0_db = esn0;
      s.seed = 900 + static_cast<std::uint64_t>(i);
      s.delay = (i % 10) / 10.0;
      s.cfo_hz = 1500.0;
      const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
      if (!d.found || !d.result.frame.header_ok) continue;
      sum += d.result.evm_snr_db;
      ++n;
    }
    REQUIRE(n >= 7);
    CHECK_NEAR(sum / n, esn0, 0.4);
  }
}


TEST(a_frame_with_a_few_errors_still_reports_its_header_and_the_crc_failure) {
  // At 8 dB there are a handful of bit errors per frame: the header (BPSK, with
  // 3 dB of extra margin) decodes, and the payload CRC says the payload is not clean.
  int crc_bad = 0, header_ok = 0;
  for (int i = 0; i < 30; ++i) {
    Scenario s;
    s.esn0_db = 8.0;
    s.seed = 2000 + static_cast<std::uint64_t>(i);
    const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
    if (!d.found || !d.result.frame.header_ok) continue;
    ++header_ok;
    if (!d.result.frame.payload_crc_ok) {
      ++crc_bad;
      CHECK(d.ber.bit_errors > 0);   // the CRC and the known payload agree that it is wrong
    } else {
      CHECK(d.ber.bit_errors == 0);
    }
  }
  CHECK(header_ok >= 28);
  CHECK(crc_bad >= 20);     // at 8 dB almost every 1056-bit frame has an error
}


MPSK_TEST_MAIN()
