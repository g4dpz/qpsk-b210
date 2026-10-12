// Tests for the RRC tap generator. Requirement 2.6 (closed form, singular points).

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "dsp/rrc.hpp"
#include "test_harness.hpp"

using mpsk::dsp::rrc_group_delay;
using mpsk::dsp::rrc_impulse;
using mpsk::dsp::rrc_taps;

namespace {

constexpr double kPi = 3.14159265358979323846;

/**
 * Value of the impulse response at a singular point, found independently of the
 * closed-form special case: the average of the general expression a small step
 * either side. The step error cancels to first order, leaving O(step^2).
 */
double limit_from_both_sides(double t, double rolloff) {
  const double step = 1e-4;
  return 0.5 * (rrc_impulse(t - step, rolloff) + rrc_impulse(t + step, rolloff));
}

bool all_finite(const std::vector<float>& v) {
  for (const float x : v) {
    if (!std::isfinite(x)) return false;
  }
  return true;
}

double energy(const std::vector<float>& v) {
  double e = 0.0;
  for (const float x : v) e += static_cast<double>(x) * static_cast<double>(x);
  return e;
}

}  // namespace

TEST(impulse_at_zero_matches_the_closed_form) {
  // h(0) = 1 - b + 4b/pi. For b = 0.35 this is 0.65 + 0.44563 = 1.09563.
  CHECK_NEAR(rrc_impulse(0.0, 0.35), 1.0 - 0.35 + 4.0 * 0.35 / kPi, 1e-12);
  CHECK_NEAR(rrc_impulse(0.0, 0.35), 1.095634, 1e-6);
  CHECK_NEAR(rrc_impulse(0.0, 0.0), 1.0, 1e-12);                  // sinc
  CHECK_NEAR(rrc_impulse(0.0, 1.0), 4.0 / kPi, 1e-12);            // 1 - 1 + 4/pi
}

TEST(impulse_at_zero_is_the_limit_of_the_general_expression) {
  for (const double b : {0.1, 0.25, 0.35, 0.5, 0.9, 1.0}) {
    CHECK_NEAR(rrc_impulse(0.0, b), limit_from_both_sides(0.0, b), 1e-6);
  }
}

TEST(impulse_at_one_over_four_b_is_the_limit_of_the_general_expression) {
  // The second singular point, where 1 - (4bt)^2 = 0.
  for (const double b : {0.1, 0.25, 0.35, 0.5, 0.9, 1.0}) {
    const double t = 1.0 / (4.0 * b);
    CHECK(std::isfinite(rrc_impulse(t, b)));
    CHECK_NEAR(rrc_impulse(t, b), limit_from_both_sides(t, b), 1e-6);
    CHECK_NEAR(rrc_impulse(-t, b), limit_from_both_sides(-t, b), 1e-6);
  }
}

TEST(impulse_is_even_in_time) {
  for (const double b : {0.0, 0.2, 0.35, 0.75, 1.0}) {
    for (const double t : {0.1, 0.5, 0.9, 1.3, 2.7, 4.2}) {
      CHECK_NEAR(rrc_impulse(t, b), rrc_impulse(-t, b), 1e-12);
    }
  }
}

TEST(impulse_with_zero_rolloff_is_sinc) {
  for (const double t : {0.25, 0.5, 1.5, 3.3}) {
    CHECK_NEAR(rrc_impulse(t, 0.0), std::sin(kPi * t) / (kPi * t), 1e-12);
  }
}

TEST(impulse_is_continuous_across_each_singular_point) {
  // A wrong special-case value would leave a visible step at the singularity.
  for (const double b : {0.25, 0.35, 0.5}) {
    const double t = 1.0 / (4.0 * b);
    const double at = rrc_impulse(t, b);
    CHECK_NEAR(rrc_impulse(t - 1e-6, b), at, 1e-4);
    CHECK_NEAR(rrc_impulse(t + 1e-6, b), at, 1e-4);
  }
  CHECK_NEAR(rrc_impulse(1e-6, 0.35), rrc_impulse(0.0, 0.35), 1e-5);
}

TEST(taps_have_the_expected_length_and_are_symmetric) {
  for (const int sps : {2, 4, 8}) {
    for (const int span : {4, 8, 11, 12}) {
      if ((span * sps) % 2 != 0) continue;
      const auto h = rrc_taps(0.35, sps, span);
      CHECK(h.size() == static_cast<std::size_t>(span * sps + 1));
      for (std::size_t i = 0; i < h.size(); ++i) {
        CHECK(h[i] == h[h.size() - 1 - i]);  // exactly symmetric
      }
    }
  }
}

TEST(taps_have_unit_energy) {
  for (const double b : {0.0, 0.2, 0.35, 0.5, 1.0}) {
    for (const int sps : {2, 4, 8}) {
      CHECK_NEAR(energy(rrc_taps(b, sps, 12)), 1.0, 1e-6);
    }
  }
}

TEST(taps_peak_at_the_centre) {
  const auto h = rrc_taps(0.35, 4, 11);
  const std::size_t centre = (h.size() - 1) / 2;
  for (std::size_t i = 0; i < h.size(); ++i) {
    if (i != centre) CHECK(std::fabs(h[i]) < h[centre]);
  }
}

TEST(taps_are_finite_even_when_the_grid_hits_a_singular_point) {
  // The second singular point is at t = 1/(4b). It lies on the sample grid
  // (k/sps) when b = sps/(4k). These cases all hit it exactly:
  //   b = 0.25, sps = 4 -> t = 1       b = 0.5,  sps = 2 -> t = 0.5
  //   b = 1.0,  sps = 4 -> t = 0.25    b = 0.125, sps = 4 -> t = 2
  struct Case {
    double b;
    int sps;
  };
  for (const Case c : {Case{0.25, 4}, Case{0.5, 2}, Case{1.0, 4}, Case{0.125, 4}, Case{0.5, 8}}) {
    const auto h = rrc_taps(c.b, c.sps, 12);
    CHECK(all_finite(h));
    CHECK_NEAR(energy(h), 1.0, 1e-6);
  }
}

TEST(singular_tap_on_the_grid_matches_the_independent_limit) {
  // b = 0.25, sps = 4: the tap k = 4 steps from the centre is t = 1 = 1/(4b).
  const int sps = 4;
  const int span = 12;
  const double b = 0.25;
  const auto h = rrc_taps(b, sps, span);
  const std::size_t centre = (h.size() - 1) / 2;

  // Undo the unit-energy scaling by comparing ratios against the centre tap.
  const double ratio = static_cast<double>(h[centre + 4]) / static_cast<double>(h[centre]);
  const double expected = limit_from_both_sides(1.0, b) / limit_from_both_sides(0.0, b);
  CHECK_NEAR(ratio, expected, 1e-5);
}

TEST(group_delay_is_the_centre_tap_index) {
  const auto h = rrc_taps(0.35, 4, 11);
  CHECK(static_cast<int>((h.size() - 1) / 2) == rrc_group_delay(4, 11));
  CHECK(rrc_group_delay(4, 11) == 22);
}

TEST(cascaded_rrc_is_a_nyquist_pulse) {
  // The transmit RRC followed by the matched RRC must be a raised-cosine pulse:
  // 1 at the centre and (nearly) 0 at every other multiple of the symbol period.
  // Truncating the filter to a finite span leaves a small residual ISI. Print
  // the worst residual so the threshold below stays tied to measured values.
  for (const double b : {0.2, 0.35, 0.5}) {
    for (const int sps : {2, 4, 8}) {
      const int span = 11 + (sps % 2);  // keep span * sps even
      const auto h = rrc_taps(b, sps, span);
      const std::size_t n = h.size();

      // Autocorrelation = convolution of the filter with its time reverse.
      // Because the taps are symmetric, that equals the convolution with itself.
      std::vector<double> rc(2 * n - 1, 0.0);
      for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) rc[i + j] += static_cast<double>(h[i]) * h[j];
      }
      const std::size_t centre = n - 1;
      CHECK_NEAR(rc[centre], 1.0, 1e-6);  // unit energy => unit centre value

      double worst = 0.0;
      for (std::size_t k = static_cast<std::size_t>(sps); k <= centre; k += static_cast<std::size_t>(sps)) {
        worst = std::max(worst, std::fabs(rc[centre + k]));
      }
      std::cout << "    beta=" << b << " sps=" << sps << " span=" << span
                << " worst ISI=" << worst << "\n";
      CHECK(worst < 0.01);  // -40 dB relative to the centre value
    }
  }
}

TEST(taps_reject_bad_arguments) {
  CHECK_THROWS(rrc_taps(-0.1, 4, 11));
  CHECK_THROWS(rrc_taps(1.1, 4, 11));
  CHECK_THROWS(rrc_taps(std::nan(""), 4, 11));
  CHECK_THROWS(rrc_taps(0.35, 1, 11));
  CHECK_THROWS(rrc_taps(0.35, 0, 11));
  CHECK_THROWS(rrc_taps(0.35, 4, 0));
}

TEST(taps_reject_an_odd_span_times_sps) {
  // 11 * 3 = 33 would give 34 taps, a centre between samples, and a half-sample delay.
  CHECK_THROWS(rrc_taps(0.35, 3, 11));
  std::string message;
  try {
    rrc_taps(0.35, 3, 11);
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(message.find("even") != std::string::npos);
  CHECK(rrc_taps(0.35, 3, 10).size() == 31);  // an even product is fine
}

TEST(rolloff_extremes_are_accepted) {
  CHECK(all_finite(rrc_taps(0.0, 4, 12)));
  CHECK(all_finite(rrc_taps(1.0, 4, 12)));
}

MPSK_TEST_MAIN()
