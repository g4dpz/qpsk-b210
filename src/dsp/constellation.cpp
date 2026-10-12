#include "dsp/constellation.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "dsp/gray.hpp"

namespace mpsk::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

/**
 * Angle of position 0, phi0.
 *
 * QPSK uses pi/4 so the four points sit at 45, 135, 225 and 315 degrees. Each
 * point then has I and Q of equal magnitude 1/sqrt(2), the usual QPSK picture,
 * and no point lies on an axis. BPSK and 8-PSK start at angle 0.
 */
double phase_offset_for(int order) { return order == 4 ? kPi / 4.0 : 0.0; }

bool is_power_of_two(int n) { return n > 1 && (n & (n - 1)) == 0; }

int log2_exact(int n) {
  int bits = 0;
  while ((1 << bits) < n) ++bits;
  return bits;
}

}  // namespace

const std::vector<int>& Constellation::supported_orders() {
  // Stage 1: QPSK only. Stage 2 adds 8 here.
  static const std::vector<int> orders{4};
  return orders;
}

Constellation::Constellation(int order) : order_(order), bits_per_symbol_(0), phase_offset_(0.0) {
  const auto& payload_orders = supported_orders();
  bool allowed = (order == 2);  // BPSK: preamble and header only
  for (const int o : payload_orders) allowed = allowed || (o == order);

  if (!allowed) {
    std::string msg;
    if (order == 8) {
      msg = "8-PSK is not enabled in this build; only QPSK is supported (M=4)";
    } else {
      msg = "unsupported modulation order M=" + std::to_string(order) + "; supported: ";
      for (std::size_t i = 0; i < payload_orders.size(); ++i) {
        if (i != 0) msg += ", ";
        msg += std::to_string(payload_orders[i]);
      }
    }
    throw std::invalid_argument(msg);
  }
  if (!is_power_of_two(order)) {
    // Unreachable while the allowed list holds only powers of two; kept so that
    // adding an order by mistake fails here instead of mapping garbage.
    throw std::invalid_argument("modulation order must be a power of two");
  }

  bits_per_symbol_ = log2_exact(order);
  phase_offset_ = phase_offset_for(order);

  // Position k carries the label gray(k), so the table is indexed by label.
  points_.resize(static_cast<std::size_t>(order));
  for (int k = 0; k < order; ++k) {
    const double angle = 2.0 * kPi * k / order + phase_offset_;
    const unsigned label = binary_to_gray(static_cast<unsigned>(k));
    points_[label] = std::complex<float>(static_cast<float>(std::cos(angle)),
                                         static_cast<float>(std::sin(angle)));
  }
}

std::complex<float> Constellation::map(unsigned label) const {
  // The mask makes an out-of-range label wrap instead of reading past the table.
  return points_[label & static_cast<unsigned>(order_ - 1)];
}

Constellation::Decision Constellation::slice(std::complex<float> y) const {
  // Position of the nearest point: angle relative to phi0, in units of the
  // point spacing 2*pi/M, rounded to the nearest whole position and wrapped
  // into 0..M-1.
  const double spacing = 2.0 * kPi / order_;
  const double angle = std::atan2(static_cast<double>(y.imag()), static_cast<double>(y.real()));
  const long k = std::lround((angle - phase_offset_) / spacing);
  const long wrapped = ((k % order_) + order_) % order_;

  const unsigned label = binary_to_gray(static_cast<unsigned>(wrapped));
  return {label, points_[label]};
}

std::vector<std::complex<float>> map_bits(const Constellation& constellation, const Bits& bits) {
  const std::size_t bps = static_cast<std::size_t>(constellation.bits_per_symbol());
  if (bits.size() % bps != 0) {
    throw std::invalid_argument("map_bits: " + std::to_string(bits.size()) +
                                " bits is not a multiple of " + std::to_string(bps) +
                                " bits per symbol");
  }
  std::vector<std::complex<float>> symbols;
  symbols.reserve(bits.size() / bps);
  for (std::size_t i = 0; i < bits.size(); i += bps) {
    unsigned label = 0;
    for (std::size_t j = 0; j < bps; ++j) label = (label << 1) | (bits[i + j] != 0 ? 1u : 0u);
    symbols.push_back(constellation.map(label));
  }
  return symbols;
}

Bits demap_symbols(const Constellation& constellation,
                   const std::vector<std::complex<float>>& symbols) {
  const int bps = constellation.bits_per_symbol();
  Bits bits;
  bits.reserve(symbols.size() * static_cast<std::size_t>(bps));
  for (const auto& y : symbols) {
    const unsigned label = constellation.demap(y);
    for (int shift = bps - 1; shift >= 0; --shift) {
      bits.push_back(static_cast<uint8_t>((label >> shift) & 1u));
    }
  }
  return bits;
}

}  // namespace mpsk::dsp
