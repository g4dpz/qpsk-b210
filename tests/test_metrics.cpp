// Tests for the link-quality measurements. Requirements 4.1, 4.2 (statistics, BER
// against a known payload), 7.11 (level), 7.12 (clipping).

#include <cmath>
#include <complex>
#include <limits>
#include <vector>

#include "dsp/constellation.hpp"
#include "dsp/metrics.hpp"
#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "rx_scenario.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cf = std::complex<float>;

// ---- EVM --------------------------------------------------------------------

TEST(a_perfect_signal_has_zero_evm_and_infinite_snr) {
  const Constellation q(4);
  EvmAccumulator evm;
  for (unsigned label = 0; label < 4; ++label) evm.add(q.map(label), q.map(label));
  CHECK(evm.count() == 4);
  CHECK(evm.rms() == 0.0);
  CHECK(std::isinf(evm.snr_db()) && evm.snr_db() > 0.0);
}

TEST(an_empty_accumulator_reads_zero) {
  EvmAccumulator evm;
  CHECK(evm.count() == 0);
  CHECK(evm.rms() == 0.0);
}

TEST(evm_is_the_rms_distance_from_the_decided_point) {
  const Constellation q(4);
  const cf d = q.map(0);
  EvmAccumulator evm;
  evm.add(d + cf(0.1f, 0.0f), d);    // distance 0.1
  evm.add(d + cf(0.0f, -0.3f), d);   // distance 0.3
  // rms = sqrt((0.01 + 0.09) / 2)
  CHECK_NEAR(evm.rms(), std::sqrt(0.05), 1e-6);
  CHECK_NEAR(evm.snr_db(), -20.0 * std::log10(std::sqrt(0.05)), 1e-5);
}

TEST(snr_from_evm_is_minus_twenty_log_evm) {
  CHECK_NEAR(evm_to_snr_db(0.1), 20.0, 1e-12);
  CHECK_NEAR(evm_to_snr_db(0.01), 40.0, 1e-12);
  CHECK_NEAR(evm_to_snr_db(1.0), 0.0, 1e-12);
  CHECK(std::isinf(evm_to_snr_db(0.0)));
  CHECK(std::isinf(evm_to_snr_db(-1.0)));
}

TEST(evm_snr_matches_the_real_snr_when_noise_is_the_only_impairment) {
  // For unit-power symbols with noise of variance 1/g, EVM = 1/sqrt(g) and the SNR read
  // back is the true symbol SNR. Measured through the whole receiver: within 0.2 dB at
  // 20, 15, 10 and 8 dB.
  const Constellation q(4);
  for (const double snr_db : {20.0, 15.0, 10.0}) {
    mpsk::sim::NoiseSource rng(1);
    const double g = std::pow(10.0, snr_db / 10.0);
    EvmAccumulator evm;
    for (int k = 0; k < 40000; ++k) {
      const cf d = q.map(static_cast<unsigned>(rng.uniform() * 4.0) & 3u);
      evm.add(d + rng.complex_noise(1.0 / g), d);
    }
    CHECK_NEAR(evm.snr_db(), snr_db, 0.15);
  }
}

TEST(evm_reads_optimistic_at_low_snr_because_wrong_decisions_look_close) {
  // The documented limit. At 0 dB a large share of decisions are wrong, and a wrong
  // decision measures the distance to the wrong point, which is smaller than the true
  // error. Here the decisions are the slicer's, not the sent symbols', as in a receiver.
  const Constellation q(4);
  mpsk::sim::NoiseSource rng(2);
  EvmAccumulator evm;
  for (int k = 0; k < 40000; ++k) {
    const cf sent = q.map(static_cast<unsigned>(rng.uniform() * 4.0) & 3u);
    const cf y = sent + rng.complex_noise(1.0);   // symbol SNR 0 dB
    evm.add(y, q.slice(y).point);
  }
  CHECK(evm.snr_db() > 1.5);    // reads well above the true 0 dB
}

// ---- Bit error rate ---------------------------------------------------------

TEST(identical_payloads_have_no_bit_errors) {
  const auto p = prbs_bytes(1, 1, 128);
  const BerResult r = payload_ber(p, p);
  CHECK(r.bit_errors == 0);
  CHECK(r.bits_compared == 1024);
  CHECK(r.rate() == 0.0);
  CHECK(!r.length_mismatch);
}

TEST(the_bit_error_count_equals_the_number_of_bits_flipped) {
  // The requirement: BER counts match injected errors. Flip a known set of bits, at
  // varied positions within bytes, and require the count to be exactly that many.
  const auto expected = prbs_bytes(2, 9, 200);
  for (const std::size_t flips : {std::size_t{1}, std::size_t{2}, std::size_t{7}, std::size_t{8}, std::size_t{50}, std::size_t{400}}) {
    auto decoded = expected;
    for (std::size_t k = 0; k < flips; ++k) {
      // Spread over the payload, each a distinct bit: bit index k * 4 + (k % 4).
      const std::size_t bit = (k * 4 + (k % 4)) % (expected.size() * 8);
      decoded[bit / 8] = static_cast<std::uint8_t>(decoded[bit / 8] ^ (0x80u >> (bit % 8)));
    }
    // Flipping the same bit twice would cancel; the pattern above is distinct for these counts.
    const BerResult r = payload_ber(decoded, expected);
    CHECK(r.bit_errors == flips);
    CHECK(r.bits_compared == 1600);
    CHECK_NEAR(r.rate(), static_cast<double>(flips) / 1600.0, 1e-12);
  }
}

TEST(a_whole_byte_of_errors_counts_eight) {
  const std::vector<std::uint8_t> a{0x00, 0xFF, 0x55};
  const std::vector<std::uint8_t> b{0xFF, 0x00, 0x55};
  const BerResult r = payload_ber(a, b);
  CHECK(r.bit_errors == 16);
  CHECK(r.bits_compared == 24);
}

TEST(a_length_mismatch_is_flagged_and_only_the_common_part_is_compared) {
  const std::vector<std::uint8_t> shorter{1, 2, 3};
  const std::vector<std::uint8_t> longer{1, 2, 3, 4, 5};
  const BerResult r = payload_ber(shorter, longer);
  CHECK(r.length_mismatch);
  CHECK(r.bits_compared == 24);
  CHECK(r.bit_errors == 0);
  CHECK(payload_ber(longer, shorter).length_mismatch);
}

TEST(empty_payloads_compare_cleanly) {
  const BerResult r = payload_ber({}, {});
  CHECK(r.bits_compared == 0);
  CHECK(r.bit_errors == 0);
  CHECK(r.rate() == 0.0);
  CHECK(!r.length_mismatch);
  CHECK(payload_ber({}, {1}).length_mismatch);
}

// ---- Level and clipping -----------------------------------------------------

TEST(a_unit_magnitude_tone_reads_zero_dbfs) {
  std::vector<cf> x(1000);
  for (std::size_t i = 0; i < x.size(); ++i) x[i] = std::polar(1.0f, 0.3f * static_cast<float>(i));
  const SignalLevel l = measure_signal_level(x.data(), x.size(), 0, x.size());
  CHECK_NEAR(l.rms_dbfs, 0.0, 1e-4);
  CHECK_NEAR(l.peak_magnitude, 1.0, 1e-5);
  CHECK(l.samples == 1000);
}

TEST(the_level_follows_amplitude_in_decibels) {
  for (const double db : {-6.0, -20.0, -40.0}) {
    const float a = static_cast<float>(std::pow(10.0, db / 20.0));
    std::vector<cf> x(500, cf(a, 0.0f));
    CHECK_NEAR(measure_signal_level(x.data(), x.size(), 0, x.size()).rms_dbfs, db, 1e-3);
  }
}

TEST(a_full_scale_square_in_both_components_reads_plus_three_dbfs) {
  // I and Q both at 1.0: |x|^2 = 2, which is 10 log10 2 = +3.01 dBFS, as documented.
  const std::vector<cf> x(100, cf(1.0f, 1.0f));
  CHECK_NEAR(measure_signal_level(x.data(), x.size(), 0, x.size()).rms_dbfs, 3.0103, 1e-3);
}

TEST(silence_reads_the_floor_value_and_an_empty_range_reads_it_too) {
  const std::vector<cf> x(100, cf(0.0f, 0.0f));
  CHECK(measure_signal_level(x.data(), x.size(), 0, x.size()).rms_dbfs == -200.0);
  CHECK(measure_signal_level(x.data(), x.size(), 10, 10).rms_dbfs == -200.0);
  CHECK(measure_signal_level(x.data(), x.size(), 50, 20).samples == 0);   // begin after end
  CHECK(measure_signal_level(nullptr, 0, 0, 10).samples == 0);
}

TEST(the_range_is_clamped_to_the_buffer) {
  const std::vector<cf> x(100, cf(0.5f, 0.0f));
  const SignalLevel l = measure_signal_level(x.data(), x.size(), 60, 5000);
  CHECK(l.samples == 40);
  CHECK_NEAR(l.rms_dbfs, 20.0 * std::log10(0.5), 1e-4);
}

TEST(only_the_requested_range_is_examined) {
  std::vector<cf> x(200, cf(0.1f, 0.0f));
  for (std::size_t i = 100; i < 150; ++i) x[i] = cf(0.9f, 0.0f);
  CHECK_NEAR(measure_signal_level(x.data(), x.size(), 100, 150).rms_dbfs, 20.0 * std::log10(0.9), 1e-4);
  CHECK_NEAR(measure_signal_level(x.data(), x.size(), 0, 100).rms_dbfs, 20.0 * std::log10(0.1), 1e-4);
}

TEST(clipping_is_counted_per_component_at_the_threshold) {
  std::vector<cf> x(10, cf(0.5f, 0.5f));
  x[2] = cf(0.99f, 0.0f);    // I beyond the threshold: counts
  x[4] = cf(0.0f, -0.99f);   // Q negative beyond it: counts
  x[6] = cf(1.0f, 1.0f);     // both: counts once
  x[8] = cf(0.97f, 0.97f);   // just under on both: does not count, even though |x| = 1.37
  const SignalLevel l = measure_signal_level(x.data(), x.size(), 0, x.size());
  CHECK(l.clipped_samples == 3);
  CHECK(l.samples == 10);
}

TEST(a_sample_exactly_at_the_threshold_counts_as_clipped) {
  // "At or above". Tested at a threshold of 0.5 with samples of exactly 0.5, which a
  // float holds without rounding, so the boundary really is on the boundary. (0.98f is
  // 0.98000002 and would pass a strict comparison too, which hid this.)
  std::vector<cf> x(4, cf(0.25f, 0.25f));
  x[1] = cf(0.5f, 0.0f);
  x[3] = cf(0.0f, -0.5f);
  CHECK(measure_signal_level(x.data(), x.size(), 0, x.size(), 0.5).clipped_samples == 2);
  // And just below it does not.
  x[1] = cf(0.49999f, 0.0f);
  CHECK(measure_signal_level(x.data(), x.size(), 0, x.size(), 0.5).clipped_samples == 1);
}

TEST(a_custom_clipping_threshold_is_used) {
  std::vector<cf> x(10, cf(0.8f, 0.0f));
  CHECK(measure_signal_level(x.data(), x.size(), 0, x.size()).clipped_samples == 0);           // default 0.98
  CHECK(measure_signal_level(x.data(), x.size(), 0, x.size(), 0.75).clipped_samples == 10);
  CHECK(kDefaultClipThreshold == 0.98);
}

TEST(a_normal_burst_does_not_clip_and_an_overdriven_one_does) {
  // The requirement: the clipping count triggers on a saturated signal. At the default
  // output scale the burst peaks at about 0.40 of full scale. Amplified by 3 it reaches
  // 1.2 and saturates. The count is taken on the raw samples, before the DC blocker.
  rx_test::Scenario s;
  s.add_noise = false;
  const auto burst = rx_test::modulator().modulate(FrameType::Data, 1, prbs_bytes(3, 1, 128));
  std::vector<cf> normal = burst.samples;
  const SignalLevel a = measure_signal_level(normal.data(), normal.size(), 0, normal.size());
  CHECK(a.clipped_samples == 0);
  CHECK(a.peak_magnitude < 0.5);

  std::vector<cf> loud = burst.samples;
  for (auto& v : loud) v *= 3.0f;
  // A hardware converter would clamp; simulate that so the count means something.
  for (auto& v : loud) v = cf(std::fmax(-1.0f, std::fmin(1.0f, v.real())), std::fmax(-1.0f, std::fmin(1.0f, v.imag())));
  const SignalLevel b = measure_signal_level(loud.data(), loud.size(), 0, loud.size());
  CHECK(b.clipped_samples > 20);
  CHECK(b.rms_dbfs > a.rms_dbfs + 6.0);
}

TEST(the_level_of_a_default_burst_is_about_minus_twelve_dbfs) {
  // Output scale 0.5, average power scale^2 / sps = 0.0625 per sample while on air,
  // which is 10 log10(0.0625) = -12.04 dBFS.
  const auto burst = rx_test::modulator().modulate(FrameType::Data, 1, prbs_bytes(3, 1, 128));
  const SignalLevel l = measure_signal_level(burst.samples.data(), burst.samples.size(), 64 + 100,
                                             burst.samples.size() - 64 - 100);
  CHECK_NEAR(l.rms_dbfs, -12.04, 0.1);
}

MPSK_TEST_MAIN()
