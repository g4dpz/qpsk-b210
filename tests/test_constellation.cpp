// Tests for the Gray code, the M-PSK constellation, the mapper and the slicer.
// Requirements 1.3 (Gray), 1.4 (unit power), 1.5 (generic in M), 1.8 (reject 8-PSK in stage 1).

#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <vector>

#include "dsp/bits.hpp"
#include "dsp/constellation.hpp"
#include "dsp/gray.hpp"
#include "test_harness.hpp"

using mpsk::dsp::binary_to_gray;
using mpsk::dsp::Bits;
using mpsk::dsp::Constellation;
using mpsk::dsp::gray_to_binary;

namespace {

constexpr double kPi = 3.14159265358979323846;

int popcount(unsigned v) {
  int n = 0;
  for (; v != 0; v >>= 1) n += static_cast<int>(v & 1u);
  return n;
}

/** Every order the constellation accepts: BPSK (internal) plus the payload orders. */
std::vector<int> orders_under_test() {
  std::vector<int> orders{2};
  for (const int o : Constellation::supported_orders()) orders.push_back(o);
  return orders;
}

}  // namespace

TEST(gray_code_round_trips) {
  for (unsigned n = 0; n < 1024; ++n) {
    CHECK(gray_to_binary(binary_to_gray(n)) == n);
  }
}

TEST(gray_code_known_values) {
  CHECK(binary_to_gray(0) == 0u);
  CHECK(binary_to_gray(1) == 1u);
  CHECK(binary_to_gray(2) == 3u);
  CHECK(binary_to_gray(3) == 2u);
  CHECK(binary_to_gray(4) == 6u);
  CHECK(binary_to_gray(5) == 7u);
  CHECK(binary_to_gray(6) == 5u);
  CHECK(binary_to_gray(7) == 4u);
}

TEST(gray_code_steps_flip_one_bit_and_wrap) {
  for (const unsigned size : {2u, 4u, 8u, 16u}) {
    for (unsigned n = 0; n < size; ++n) {
      const unsigned next = (n + 1) % size;  // wraps from size-1 back to 0
      CHECK(popcount(binary_to_gray(n) ^ binary_to_gray(next)) == 1);
    }
  }
}

TEST(constellation_has_unit_average_power) {
  for (const int order : orders_under_test()) {
    const Constellation c(order);
    double power = 0.0;
    for (unsigned label = 0; label < static_cast<unsigned>(order); ++label) {
      const std::complex<float> p = c.map(label);
      power += static_cast<double>(std::norm(p));
      CHECK_NEAR(std::abs(p), 1.0, 1e-6);  // every point is on the unit circle
    }
    CHECK_NEAR(power / order, 1.0, 1e-6);
  }
}

TEST(qpsk_points_are_at_45_135_225_315_degrees) {
  const Constellation c(4);
  const float a = static_cast<float>(1.0 / std::sqrt(2.0));
  // Position k = 0..3 sits at 45, 135, 225, 315 degrees and carries label gray(k).
  CHECK_NEAR(c.map(0b00).real(), +a, 1e-6);  // 45 degrees
  CHECK_NEAR(c.map(0b00).imag(), +a, 1e-6);
  CHECK_NEAR(c.map(0b01).real(), -a, 1e-6);  // 135 degrees
  CHECK_NEAR(c.map(0b01).imag(), +a, 1e-6);
  CHECK_NEAR(c.map(0b11).real(), -a, 1e-6);  // 225 degrees
  CHECK_NEAR(c.map(0b11).imag(), -a, 1e-6);
  CHECK_NEAR(c.map(0b10).real(), +a, 1e-6);  // 315 degrees
  CHECK_NEAR(c.map(0b10).imag(), -a, 1e-6);
}

TEST(bpsk_points_are_plus_and_minus_one) {
  const Constellation c(2);
  CHECK_NEAR(c.map(0).real(), 1.0, 1e-6);
  CHECK_NEAR(c.map(0).imag(), 0.0, 1e-6);
  CHECK_NEAR(c.map(1).real(), -1.0, 1e-6);
  CHECK_NEAR(c.map(1).imag(), 0.0, 1e-6);
}

TEST(nearest_neighbours_differ_by_exactly_one_bit) {
  // Checked on geometry, not on the construction: for every pair of points at
  // the minimum distance, the labels must differ in one bit.
  for (const int order : orders_under_test()) {
    const Constellation c(order);
    const unsigned n = static_cast<unsigned>(order);

    double min_dist = 1e9;
    for (unsigned a = 0; a < n; ++a) {
      for (unsigned b = a + 1; b < n; ++b) {
        min_dist = std::min(min_dist, static_cast<double>(std::abs(c.map(a) - c.map(b))));
      }
    }
    int neighbour_pairs = 0;
    for (unsigned a = 0; a < n; ++a) {
      for (unsigned b = a + 1; b < n; ++b) {
        const double d = static_cast<double>(std::abs(c.map(a) - c.map(b)));
        if (std::fabs(d - min_dist) < 1e-4) {
          ++neighbour_pairs;
          CHECK(popcount(a ^ b) == 1);
        }
      }
    }
    // Points on a circle: each point has two neighbours, giving M pairs. BPSK
    // has only the one pair.
    CHECK(neighbour_pairs == (order == 2 ? 1 : order));
  }
}

TEST(map_then_demap_returns_every_label) {
  for (const int order : orders_under_test()) {
    const Constellation c(order);
    for (unsigned label = 0; label < static_cast<unsigned>(order); ++label) {
      const auto decision = c.slice(c.map(label));
      CHECK(decision.label == label);
      CHECK_NEAR(std::abs(decision.point - c.map(label)), 0.0, 1e-6);
    }
  }
}

TEST(slicer_ignores_amplitude_and_tolerates_rotation_up_to_half_spacing) {
  for (const int order : orders_under_test()) {
    const Constellation c(order);
    const double half_spacing = kPi / order;
    for (unsigned label = 0; label < static_cast<unsigned>(order); ++label) {
      for (const double amplitude : {0.05, 1.0, 40.0}) {
        for (const double fraction : {-0.9, -0.5, 0.0, 0.5, 0.9}) {
          const std::complex<double> rot = std::polar(amplitude, fraction * half_spacing);
          const std::complex<double> p(c.map(label).real(), c.map(label).imag());
          const std::complex<double> y = p * rot;
          CHECK(c.demap(std::complex<float>(static_cast<float>(y.real()),
                                            static_cast<float>(y.imag()))) == label);
        }
      }
    }
  }
}

TEST(slicer_crossing_the_boundary_picks_a_neighbour_with_one_bit_difference) {
  for (const int order : orders_under_test()) {
    const Constellation c(order);
    const double half_spacing = kPi / order;
    for (unsigned label = 0; label < static_cast<unsigned>(order); ++label) {
      for (const double sign : {-1.0, 1.0}) {
        const std::complex<double> p(c.map(label).real(), c.map(label).imag());
        const std::complex<double> y = p * std::polar(1.0, sign * 1.1 * half_spacing);
        const unsigned decided = c.demap(
            std::complex<float>(static_cast<float>(y.real()), static_cast<float>(y.imag())));
        CHECK(decided != label);
        CHECK(popcount(decided ^ label) == 1);  // a slip to a neighbour costs one bit
      }
    }
  }
}

TEST(bits_map_and_demap_round_trip) {
  std::mt19937 rng(12345);
  for (const int order : orders_under_test()) {
    const Constellation c(order);
    const std::size_t bps = static_cast<std::size_t>(c.bits_per_symbol());
    Bits bits(bps * 1000);
    for (auto& b : bits) b = static_cast<uint8_t>(rng() & 1u);

    const auto symbols = mpsk::dsp::map_bits(c, bits);
    CHECK(symbols.size() == 1000);
    CHECK(mpsk::dsp::demap_symbols(c, symbols) == bits);
  }
}

TEST(bits_are_taken_most_significant_first) {
  const Constellation c(4);
  // Bits 0,1 form label 0b01 -> position 1 -> 135 degrees.
  const auto s = mpsk::dsp::map_bits(c, Bits{0, 1});
  CHECK(std::abs(s[0] - c.map(0b01)) < 1e-6f);
  // Bits 1,0 form label 0b10 -> position 3 -> 315 degrees.
  const auto t = mpsk::dsp::map_bits(c, Bits{1, 0});
  CHECK(std::abs(t[0] - c.map(0b10)) < 1e-6f);
}

TEST(bits_per_symbol_matches_order) {
  CHECK(Constellation(2).bits_per_symbol() == 1);
  CHECK(Constellation(4).bits_per_symbol() == 2);
}

TEST(map_bits_rejects_a_partial_symbol) {
  const Constellation c(4);
  CHECK_THROWS(mpsk::dsp::map_bits(c, Bits{1, 0, 1}));
}

TEST(stage_one_accepts_qpsk_only_for_payloads) {
  const auto& orders = Constellation::supported_orders();
  CHECK(orders.size() == 1);
  CHECK(orders.front() == 4);
}

TEST(unsupported_orders_are_rejected_with_a_clear_message) {
  CHECK_THROWS(Constellation(8));
  CHECK_THROWS(Constellation(16));
  CHECK_THROWS(Constellation(3));
  CHECK_THROWS(Constellation(1));
  CHECK_THROWS(Constellation(0));
  CHECK_THROWS(Constellation(-4));

  std::string message;
  try {
    Constellation c(8);
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(message.find("8-PSK") != std::string::npos);
  CHECK(message.find("QPSK") != std::string::npos);
}

MPSK_TEST_MAIN()
