// Tests for the spectrum and occupied-bandwidth measurement, and the narrowband
// profile's bandwidth limit (task 10A.2, Requirement 12.2).

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "dsp/spectrum.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cd = std::complex<double>;
using cf = std::complex<float>;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<cd> naive_dft(const std::vector<cd>& x) {
  const std::size_t n = x.size();
  std::vector<cd> out(n);
  for (std::size_t k = 0; k < n; ++k) {
    cd acc(0, 0);
    for (std::size_t j = 0; j < n; ++j) {
      acc += x[j] * std::polar(1.0, -2.0 * kPi * static_cast<double>(k * j % n) / static_cast<double>(n));
    }
    out[k] = acc;
  }
  return out;
}

std::vector<cd> random_vector(std::size_t n, std::uint64_t seed) {
  mpsk::sim::NoiseSource rng(seed);
  std::vector<cd> v(n);
  for (auto& x : v) x = cd(rng.gaussian(), rng.gaussian());
  return v;
}

/** A signal with a flat spectrum from -width/2 to +width/2 and nothing outside it. */
std::vector<cf> flat_band(double width_hz, double sample_rate_hz, std::size_t n, std::uint64_t seed) {
  mpsk::sim::NoiseSource rng(seed);
  std::vector<cd> spectrum(n, cd(0, 0));
  const double df = sample_rate_hz / static_cast<double>(n);
  const long half_bins = static_cast<long>(std::floor(0.5 * width_hz / df));
  for (long k = -half_bins; k <= half_bins; ++k) {
    spectrum[static_cast<std::size_t>((k + static_cast<long>(n)) % static_cast<long>(n))] =
        std::polar(1.0, 2.0 * kPi * rng.uniform());
  }
  fft(spectrum, true);
  std::vector<cf> x(n);
  const double scale = std::sqrt(static_cast<double>(n));   // roughly unit power
  for (std::size_t i = 0; i < n; ++i) x[i] = cf(static_cast<float>(spectrum[i].real() * scale), static_cast<float>(spectrum[i].imag() * scale));
  return x;
}

/** A signal whose spectrum has the given amplitude at each frequency (random phases), periodic in @p n samples. */
template <class AmpOfHz>
std::vector<cf> shaped(AmpOfHz amp_of_hz, double sample_rate_hz, std::size_t n, std::uint64_t seed,
                       double extra_line_hz = 0.0, double extra_line_amp = 0.0) {
  mpsk::sim::NoiseSource rng(seed);
  std::vector<cd> spectrum(n, cd(0, 0));
  const double df = sample_rate_hz / static_cast<double>(n);
  for (long k = -static_cast<long>(n / 2) + 1; k < static_cast<long>(n / 2); ++k) {
    const double a = amp_of_hz(static_cast<double>(k) * df);
    if (a > 0.0) spectrum[static_cast<std::size_t>((k + static_cast<long>(n)) % static_cast<long>(n))] = std::polar(a, 2.0 * kPi * rng.uniform());
  }
  if (extra_line_amp > 0.0) {
    const long k = std::lround(extra_line_hz / df);
    spectrum[static_cast<std::size_t>((k + static_cast<long>(n)) % static_cast<long>(n))] = std::polar(extra_line_amp, 0.3);
  }
  fft(spectrum, true);
  std::vector<cf> x(n);
  for (std::size_t i = 0; i < n; ++i) x[i] = cf(static_cast<float>(spectrum[i].real() * 256.0), static_cast<float>(spectrum[i].imag() * 256.0));
  return x;
}

/** Back-to-back QPSK frames from the modulator at the given pulse, with its default guard. */
std::vector<cf> modulated(double rolloff, int span, int frames = 60) {
  ModulatorConfig mc;
  mc.rolloff = rolloff;
  mc.span_symbols = span;
  const Modulator m(mc);
  std::vector<cf> x;
  for (int i = 1; i <= frames; ++i) {
    const auto burst = m.modulate(FrameType::Data, static_cast<std::uint16_t>(i), prbs_bytes(5, static_cast<std::uint32_t>(i), 128));
    x.insert(x.end(), burst.samples.begin(), burst.samples.end());
  }
  return x;
}

}  // namespace

// ---- The FFT ----------------------------------------------------------------

TEST(the_fft_matches_the_definition) {
  for (const std::size_t n : {std::size_t{2}, std::size_t{4}, std::size_t{16}, std::size_t{64}}) {
    const std::vector<cd> x = random_vector(n, 3 + n);
    std::vector<cd> y = x;
    fft(y);
    const std::vector<cd> want = naive_dft(x);
    for (std::size_t k = 0; k < n; ++k) CHECK_NEAR(std::abs(y[k] - want[k]), 0.0, 1e-9);
  }
}

TEST(the_inverse_fft_undoes_the_fft) {
  for (const std::size_t n : {std::size_t{8}, std::size_t{1024}, std::size_t{65536}}) {
    const std::vector<cd> x = random_vector(n, 11);
    std::vector<cd> y = x;
    fft(y);
    fft(y, true);
    double worst = 0;
    for (std::size_t k = 0; k < n; ++k) worst = std::max(worst, std::abs(y[k] - x[k]));
    CHECK(worst < 1e-10);
  }
}

TEST(the_fft_conserves_energy_and_puts_a_tone_in_one_bin) {
  const std::vector<cd> x = random_vector(256, 5);
  std::vector<cd> y = x;
  fft(y);
  double ex = 0, ey = 0;
  for (std::size_t k = 0; k < 256; ++k) { ex += std::norm(x[k]); ey += std::norm(y[k]); }
  CHECK_NEAR(ey / 256.0, ex, 1e-9 * ex);   // Parseval

  std::vector<cd> tone(32);
  for (std::size_t n = 0; n < 32; ++n) tone[n] = std::polar(1.0, 2.0 * kPi * 3.0 * static_cast<double>(n) / 32.0);
  fft(tone);
  for (std::size_t k = 0; k < 32; ++k) CHECK_NEAR(std::abs(tone[k]), k == 3 ? 32.0 : 0.0, 1e-9);
}

TEST(the_fft_handles_the_trivial_sizes_and_refuses_the_rest) {
  std::vector<cd> empty, one = {cd(2, 3)};
  fft(empty);
  fft(one);
  CHECK(empty.empty());
  CHECK(one[0] == cd(2, 3));
  std::vector<cd> twelve(12);
  CHECK_THROWS(fft(twelve));
  std::vector<cd> six(6);
  CHECK_THROWS(fft(six, true));
}

// ---- The spectrum estimate --------------------------------------------------

TEST(a_white_signal_has_a_flat_spectrum_and_the_segment_count_is_right) {
  mpsk::sim::NoiseSource rng(9);
  std::vector<cf> x(40960, cf(0, 0));
  mpsk::sim::add_noise(x, 1.0, rng);
  SpectrumConfig c;
  c.fft_size = 1024;
  std::size_t segments = 0;
  const std::vector<double> psd = welch_psd(x, c, &segments);
  CHECK(segments == (40960 - 1024) / 512 + 1);
  REQUIRE(psd.size() == 1024);
  double lo = 0, hi = 0;
  for (std::size_t k = 0; k < 512; ++k) { lo += psd[k]; hi += psd[512 + k]; }
  CHECK_NEAR(lo / hi, 1.0, 0.05);
}

TEST(the_spectrum_refuses_what_it_cannot_measure) {
  std::vector<cf> x(5000, cf(1, 0));
  SpectrumConfig bad;
  bad.fft_size = 1000;
  CHECK_THROWS(welch_psd(x, bad));
  bad.fft_size = 32;                 // under the minimum
  CHECK_THROWS(welch_psd(x, bad));
  SpectrumConfig even;
  even.smoothing_bins = 8;
  CHECK_THROWS(measure_occupied_bandwidth(x, 8000.0, 2000.0, even));
  even.smoothing_bins = 0;
  CHECK_THROWS(measure_occupied_bandwidth(x, 8000.0, 2000.0, even));
  CHECK_THROWS(welch_psd(std::vector<cf>(100, cf(0, 0)), SpectrumConfig{}));   // shorter than one FFT
  CHECK_THROWS(measure_occupied_bandwidth(x, 0.0, 2000.0));
  CHECK_THROWS(measure_occupied_bandwidth(x, -8000.0, 2000.0));
  CHECK_THROWS(measure_occupied_bandwidth(x, 8000.0, 0.0));
  CHECK_THROWS(measure_occupied_bandwidth(x, std::nan(""), 2000.0));
  CHECK_THROWS(measure_occupied_bandwidth(x, 8000.0, std::numeric_limits<double>::infinity()));
}

// ---- The bandwidth measurement against signals of known width --------------

TEST(white_noise_occupies_the_whole_band) {
  mpsk::sim::NoiseSource rng(4);
  std::vector<cf> x(200000, cf(0, 0));
  mpsk::sim::add_noise(x, 1.0, rng);
  const OccupiedBandwidth b = measure_occupied_bandwidth(x, 8000.0, 2000.0);
  CHECK_NEAR(b.power_99_hz, 0.99 * 8000.0, 100.0);
  CHECK_NEAR(b.power_999_hz, 0.999 * 8000.0, 100.0);
  CHECK_NEAR(b.width_26_db_hz, 8000.0, 5.0);
  CHECK_NEAR(b.width_40_db_hz, 8000.0, 5.0);
  CHECK_NEAR(b.resolution_hz, 8000.0 / 4096.0, 1e-12);
  CHECK(b.segments == (200000 - 4096) / 2048 + 1);
}

TEST(a_flat_band_of_known_width_is_measured_at_that_width) {
  for (const double width : {1000.0, 2000.0, 3000.0}) {
    const std::vector<cf> x = flat_band(width, 8000.0, 65536, 7);
    SpectrumConfig c;
    c.fft_size = 2048;
    const OccupiedBandwidth b = measure_occupied_bandwidth(x, 8000.0, width, c);   // reference region inside the band
    // The skirt widths read a little wide: smoothing over 9 bins carries the edge up to 4 bins
    // (16 Hz here) beyond where it is, on each side.
    CHECK_NEAR(b.power_99_hz, 0.99 * width, 0.02 * width + 10.0);
    CHECK_NEAR(b.width_26_db_hz, width + 30.0, 0.02 * width + 25.0);
    CHECK_NEAR(b.width_40_db_hz, width + 30.0, 0.04 * width + 40.0);
    CHECK(b.width_26_db_hz >= width - 5.0);        // never narrower than the signal
    CHECK(b.width_40_db_hz >= b.width_26_db_hz);
    CHECK(b.power_999_hz >= b.power_99_hz);
  }
}

TEST(the_widths_do_not_depend_on_the_level_of_the_signal) {
  std::vector<cf> x = flat_band(2000.0, 8000.0, 65536, 7);
  SpectrumConfig c;
  c.fft_size = 2048;
  const OccupiedBandwidth a = measure_occupied_bandwidth(x, 8000.0, 2000.0, c);
  for (auto& v : x) v *= 37.0f;
  const OccupiedBandwidth b = measure_occupied_bandwidth(x, 8000.0, 2000.0, c);
  CHECK_NEAR(a.power_99_hz, b.power_99_hz, 1e-9);
  CHECK_NEAR(a.width_26_db_hz, b.width_26_db_hz, 1e-9);
  CHECK_NEAR(a.width_40_db_hz, b.width_40_db_hz, 1e-9);
}

TEST(the_widths_follow_the_sample_rate) {
  // The same signal described at twice the sample rate is twice as wide in hertz.
  const std::vector<cf> x = flat_band(2000.0, 8000.0, 65536, 7);
  SpectrumConfig c;
  c.fft_size = 2048;
  const OccupiedBandwidth a = measure_occupied_bandwidth(x, 8000.0, 2000.0, c);
  const OccupiedBandwidth b = measure_occupied_bandwidth(x, 16000.0, 4000.0, c);
  CHECK_NEAR(b.power_99_hz, 2.0 * a.power_99_hz, 1e-9);
  CHECK_NEAR(b.width_26_db_hz, 2.0 * a.width_26_db_hz, 1e-9);
  CHECK_NEAR(b.resolution_hz, 2.0 * a.resolution_hz, 1e-12);
}

// ---- The narrowband profile -------------------------------------------------

TEST(the_profile_signal_fits_the_2700_hz_channel) {
  // 2000 Bd, roll-off 0.25, 4 samples per symbol: the DSP runs at 8 kS/s. Requirement 12.2.
  // Measured when this was written (span 11 / span 21): 99% power 2200 / 2220 Hz, -26 dB
  // 2502 / 2473 Hz, -40 dB 3090 / 2529 Hz.
  for (const int span : {11, 21}) {
    const std::vector<cf> x = modulated(0.25, span);
    const OccupiedBandwidth b = measure_occupied_bandwidth(x, 8000.0, 2000.0);
    CHECK(b.segments > 40);
    CHECK(b.width_26_db_hz <= 2600.0);                 // 100 Hz inside the 2.7 kHz limit
    CHECK(b.width_26_db_hz > 2350.0);                  // and not by being a different signal
    CHECK_NEAR(b.power_99_hz, 2210.0, 60.0);
    CHECK_NEAR(b.power_999_hz, 2370.0, 80.0);
  }
}

TEST(a_longer_pulse_pulls_in_the_far_skirts) {
  // The reason the profile uses a span of 21: the -40 dB width is the truncation of the
  // pulse, and at span 11 it is wider than the channel while at span 21 it is not.
  const OccupiedBandwidth s11 = measure_occupied_bandwidth(modulated(0.25, 11), 8000.0, 2000.0);
  const OccupiedBandwidth s21 = measure_occupied_bandwidth(modulated(0.25, 21), 8000.0, 2000.0);
  CHECK(s11.width_40_db_hz > 2700.0);
  CHECK(s21.width_40_db_hz < 2700.0);
  CHECK(s21.width_40_db_hz < s11.width_40_db_hz - 300.0);
}

TEST(a_roll_off_of_0_35_leaves_no_margin_at_2000_baud) {
  // Why the profile is 0.25: at 0.35 the -26 dB width is within 50 Hz of the limit.
  const OccupiedBandwidth b = measure_occupied_bandwidth(modulated(0.35, 21), 8000.0, 2000.0);
  CHECK(b.width_26_db_hz > 2600.0);
  CHECK(b.width_26_db_hz < 2700.0 + 40.0);
}

TEST(the_width_scales_with_the_symbol_rate_not_the_sample_rate) {
  // Same samples, described at 250 kBd (1 MS/s): the widths are 125 times larger in hertz.
  const std::vector<cf> x = modulated(0.25, 21);
  const OccupiedBandwidth narrow = measure_occupied_bandwidth(x, 8000.0, 2000.0);
  const OccupiedBandwidth wide = measure_occupied_bandwidth(x, 1e6, 250000.0);
  CHECK_NEAR(wide.width_26_db_hz / narrow.width_26_db_hz, 125.0, 0.01);
  CHECK_NEAR(wide.power_99_hz / narrow.power_99_hz, 125.0, 0.01);
}

// ---- Properties of the measurement itself -----------------------------------

TEST(the_power_width_counts_both_sides_of_zero_hertz) {
  // Power on [0, +1000] Hz and on [-2000, 0] Hz. The narrowest symmetric band holding 99% must
  // reach nearly -2000 to take in the negative side: about 2 x 1970 Hz. Counting only the
  // positive frequencies would give about 2 x 990.
  const std::vector<cf> x = shaped([](double f) { return (f >= 0 && f <= 1000) || (f < 0 && f >= -2000) ? 1.0 : 0.0; },
                                   8000.0, 65536, 3);
  SpectrumConfig c;
  c.fft_size = 2048;
  const OccupiedBandwidth b = measure_occupied_bandwidth(x, 8000.0, 1000.0, c);
  CHECK_NEAR(b.power_99_hz, 3940.0, 60.0);
  CHECK_NEAR(b.power_999_hz, 3990.0, 60.0);
}

TEST(the_reference_level_is_taken_from_the_core_of_the_band) {
  // A core to +-300 Hz at amplitude 1, with shoulders to +-1000 Hz at amplitude 0.04 (28 dB down).
  // With the symbol rate 1000 Hz the reference region is the core, 28 dB is below the 26 dB
  // threshold, and the width is the core's, about 640 Hz. If the region reached into the shoulders
  // the reference would fall by 3 dB, the shoulders would count, and the width would be 2000 Hz.
  const std::vector<cf> x = shaped([](double f) { return std::fabs(f) <= 300.0 ? 1.0 : (std::fabs(f) <= 1000.0 ? 0.04 : 0.0); },
                                   8000.0, 65536, 3);
  SpectrumConfig c;
  c.fft_size = 2048;
  CHECK(measure_occupied_bandwidth(x, 8000.0, 1000.0, c).width_26_db_hz < 800.0);
  CHECK(measure_occupied_bandwidth(x, 8000.0, 2000.0, c).width_26_db_hz > 1900.0);   // reference region +-600 Hz
}

TEST(a_single_weak_spur_is_averaged_with_its_neighbours_and_does_not_set_the_width) {
  // A flat band to +-1000 Hz and one line at 3000 Hz that is a little above the -26 dB threshold
  // in its own bin. Smoothed over 9 bins it is seen about 8 dB weaker, below the threshold: the
  // width stays the band's. Unsmoothed it counts and the width jumps to 6000 Hz. So this measures
  // the skirts of the modulation. Discrete spurs, which need a spectrum analyser, can hide here.
  const std::vector<cf> x = shaped([](double f) { return std::fabs(f) <= 1000.0 ? 1.0 : 0.0; }, 8000.0, 65536, 3, 3000.0, 0.5);
  SpectrumConfig smoothed, raw;
  smoothed.fft_size = raw.fft_size = 2048;
  raw.smoothing_bins = 1;
  CHECK(measure_occupied_bandwidth(x, 8000.0, 2000.0, smoothed).width_26_db_hz < 2100.0);
  CHECK(measure_occupied_bandwidth(x, 8000.0, 2000.0, raw).width_26_db_hz > 5900.0);
}

TEST(a_record_of_exactly_one_fft_is_enough_and_one_sample_less_is_not) {
  SpectrumConfig c;
  c.fft_size = 256;
  CHECK(welch_psd(std::vector<cf>(256, cf(1, 0)), c).size() == 256);
  CHECK_THROWS(welch_psd(std::vector<cf>(255, cf(1, 0)), c));
  std::size_t segments = 0;
  welch_psd(std::vector<cf>(256, cf(1, 0)), c, &segments);
  CHECK(segments == 1);
  welch_psd(std::vector<cf>(511, cf(1, 0)), c, &segments);
  CHECK(segments == 2);                       // starts at 0 and 128; a start at 256 would run off the end
  welch_psd(std::vector<cf>(512, cf(1, 0)), c, &segments);
  CHECK(segments == 3);                       // 50% overlap: starts at 0, 128, 256
}

TEST(a_sample_rate_of_infinity_is_refused) {
  const std::vector<cf> x(5000, cf(1, 0));
  CHECK_THROWS(measure_occupied_bandwidth(x, std::numeric_limits<double>::infinity(), 2000.0));
}

MPSK_TEST_MAIN()
