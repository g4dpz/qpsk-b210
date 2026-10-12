#include "dsp/preamble_detector.hpp"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dsp/interpolator.hpp"

namespace mpsk::dsp {

namespace {

using cd = std::complex<double>;
using cf = std::complex<float>;

constexpr int kN = kPreambleSymbols;                // 64
constexpr int kSeg = kPreambleSegmentSymbols;       // 16

/** The preamble as +1 / -1 (it is real BPSK). */
const std::array<double, kN>& signs() {
  static const std::array<double, kN> s = [] {
    std::array<double, kN> out{};
    const auto& sym = preamble_symbols();
    for (int i = 0; i < kN; ++i) out[static_cast<std::size_t>(i)] = static_cast<double>(sym[static_cast<std::size_t>(i)].real());
    return out;
  }();
  return s;
}

/** Everything computed from 64 symbol-spaced samples. */
struct Analysis {
  cd c[kPreambleSegments];   ///< segment correlations
  double energy = 0.0;       ///< sum of |z_i|^2
  double omega = 0.0;        ///< Luise-Reggiannini frequency estimate, rad/symbol
  cd coherent{0.0, 0.0};     ///< sum z_i s_i exp(-j omega (i - 31.5))
};

/**
 * The cheap half: segment correlations and energy only. This is all stage 1 needs,
 * and it runs at every sample position, so it must stay light. The preamble is real
 * +-1, so each term is an add or a subtract, with no complex multiply.
 */
Analysis segment_sums(const cf* z) {
  const auto& s = signs();
  Analysis a;
  for (int seg = 0; seg < kPreambleSegments; ++seg) {
    double re = 0.0;
    double im = 0.0;
    double energy = 0.0;
    for (int j = 0; j < kSeg; ++j) {
      const int i = seg * kSeg + j;
      const double zr = static_cast<double>(z[i].real());
      const double zi = static_cast<double>(z[i].imag());
      const double sign = s[static_cast<std::size_t>(i)];
      re += sign * zr;
      im += sign * zi;
      energy += zr * zr + zi * zi;
    }
    a.c[seg] = cd(re, im);
    a.energy += energy;
  }
  return a;
}

/** The full measurement: segment sums, then the frequency and the coherent sum. Stage 2 only. */
Analysis analyse(const cf* z) {
  const auto& s = signs();
  Analysis a = segment_sums(z);

  // Frequency: each segment correlation carries the carrier phase at its own
  // centre, and the centres are 16 symbols apart, so the product of one with the
  // conjugate of the previous has phase 16 * omega. Summing the three products
  // averages the noise before taking the angle.
  cd acc(0.0, 0.0);
  for (int k = 1; k < kPreambleSegments; ++k) acc += a.c[k] * std::conj(a.c[k - 1]);
  a.omega = (std::abs(acc) > 0.0) ? std::arg(acc) / static_cast<double>(kSeg) : 0.0;

  // Coherent sum with that frequency removed, referenced to the preamble centre.
  for (int i = 0; i < kN; ++i) {
    const cd zi(static_cast<double>(z[i].real()), static_cast<double>(z[i].imag()));
    a.coherent += zi * s[static_cast<std::size_t>(i)] *
                  std::polar(1.0, -a.omega * (static_cast<double>(i) - kPreambleCentre));
  }
  return a;
}

/** Gathers the 64 symbol-spaced samples starting at integer position t. */
void gather(const cf* y, std::size_t t, int sps, cf* z) {
  for (int i = 0; i < kN; ++i) z[i] = y[t + static_cast<std::size_t>(i) * static_cast<std::size_t>(sps)];
}

double coarse_from(const Analysis& a) {
  if (a.energy <= 1e-30) return 0.0;
  double sum = 0.0;
  for (const cd& c : a.c) sum += std::norm(c);
  return sum / (static_cast<double>(kSeg) * a.energy);
}

void validate(const AcquisitionConfig& config) {
  if (config.samples_per_symbol < 2) {
    throw std::invalid_argument("AcquisitionConfig: samples per symbol must be at least 2, got " +
                                std::to_string(config.samples_per_symbol));
  }
  if (!(config.coarse_threshold > 0.0 && config.coarse_threshold <= 1.0)) {
    throw std::invalid_argument("AcquisitionConfig: coarse threshold must be in (0, 1], got " +
                                std::to_string(config.coarse_threshold));
  }
  if (!(config.verify_threshold > 0.0 && config.verify_threshold <= 1.0)) {
    throw std::invalid_argument("AcquisitionConfig: verify threshold must be in (0, 1], got " +
                                std::to_string(config.verify_threshold));
  }
}

}  // namespace

std::size_t preamble_span_samples(int sps) {
  return static_cast<std::size_t>(kN - 1) * static_cast<std::size_t>(sps) + 1;
}

std::size_t acquisition_margin(int sps) {
  // Stage 2 searches +-sps/2 samples around the coarse peak, the parabola needs
  // one more on each side, and the interpolation kernel reads kDefaultInterpolationHalfWidth
  // beyond the position it is asked for. One extra for the rounding of the position.
  return static_cast<std::size_t>(sps / 2) + 1 + static_cast<std::size_t>(kDefaultInterpolationHalfWidth) + 1;
}

double coarse_metric(const cf* y, std::size_t n, std::size_t t, int sps) {
  if (sps < 2) throw std::invalid_argument("coarse_metric: samples per symbol must be at least 2");
  if (t + preamble_span_samples(sps) > n) {
    throw std::invalid_argument("coarse_metric: position " + std::to_string(t) + " leaves too few samples");
  }
  cf z[kN];
  gather(y, t, sps, z);
  return coarse_from(segment_sums(z));
}

std::optional<PreambleEstimate> estimate_preamble(const cf* y, std::size_t n, std::size_t t_coarse,
                                                  const AcquisitionConfig& config) {
  validate(config);
  const int sps = config.samples_per_symbol;
  const std::size_t margin = acquisition_margin(sps);
  if (t_coarse < margin || t_coarse + preamble_span_samples(sps) + margin > n) return std::nullopt;

  // Integer search: for each position, its own frequency estimate and the coherent
  // magnitude with that frequency removed. The best magnitude marks the timing.
  const long half = sps / 2;
  const long lo = static_cast<long>(t_coarse) - half - 1;
  const long hi = static_cast<long>(t_coarse) + half + 1;
  std::vector<double> magnitude(static_cast<std::size_t>(hi - lo + 1), 0.0);
  std::vector<double> coarse(magnitude.size(), 0.0);
  cf z[kN];
  for (long u = lo; u <= hi; ++u) {
    gather(y, static_cast<std::size_t>(u), sps, z);
    const Analysis a = analyse(z);
    magnitude[static_cast<std::size_t>(u - lo)] = std::abs(a.coherent);
    coarse[static_cast<std::size_t>(u - lo)] = coarse_from(a);
  }

  // The parabola needs a neighbour on each side, so the winner is chosen from the
  // inner positions only.
  std::size_t best = 1;
  for (std::size_t i = 1; i + 1 < magnitude.size(); ++i) {
    if (magnitude[i] > magnitude[best]) best = i;
  }

  // Fractional timing: vertex of the parabola through the three magnitudes around
  // the best position. With a = m[-1], b = m[0], c = m[+1], the vertex is at
  // delta = (a - c) / (2 (a - 2b + c)). It is only meaningful for a peak (a - 2b + c < 0);
  // otherwise, and for a vertex outside +-0.5, stay within half a sample.
  double delta = 0.0;
  const double a = magnitude[best - 1];
  const double b = magnitude[best];
  const double c = magnitude[best + 1];
  const double curvature = a - 2.0 * b + c;
  if (curvature < 0.0) {
    delta = 0.5 * (a - c) / curvature;
    if (delta > 0.5) delta = 0.5;
    if (delta < -0.5) delta = -0.5;
  }
  const double position = static_cast<double>(lo + static_cast<long>(best)) + delta;

  // Re-measure at the refined position, reading the signal between samples.
  for (int i = 0; i < kN; ++i) {
    z[i] = sinc_interpolate(y, n, position + static_cast<double>(i) * static_cast<double>(sps));
  }
  const Analysis fine = analyse(z);

  PreambleEstimate e;
  e.position = position;
  e.omega = fine.omega;
  e.phase_centre = std::arg(fine.coherent);
  e.amplitude = std::abs(fine.coherent) / static_cast<double>(kN);
  e.coarse_metric = coarse[best];
  e.verify_metric = (fine.energy > 1e-30)
                        ? std::norm(fine.coherent) / (static_cast<double>(kN) * fine.energy)
                        : 0.0;
  return e;
}

std::optional<PreambleEstimate> acquire_preamble(const cf* y, std::size_t n, std::size_t start,
                                                 const AcquisitionConfig& config,
                                                 AcquisitionStats* stats) {
  validate(config);
  const int sps = config.samples_per_symbol;
  const std::size_t span = preamble_span_samples(sps);
  const std::size_t margin = acquisition_margin(sps);
  if (stats) *stats = AcquisitionStats{};

  // Last position that still leaves room for stage 2 after it.
  if (n < span + 2 * margin) return std::nullopt;
  const std::size_t last = n - span - margin;

  // Stage 1 finds the peak within this many positions of the first crossing. A
  // crossing happens up to about 2.5 samples before the true peak at 4 samples
  // per symbol, so two symbols' worth leaves room.
  const std::size_t peak_window = static_cast<std::size_t>(2 * sps);

  std::size_t t = (start > margin) ? start : margin;
  while (t <= last) {
    if (stats) ++stats->positions_scanned;
    if (coarse_metric(y, n, t, sps) < config.coarse_threshold) {
      ++t;
      continue;
    }
    if (stats) ++stats->candidates;

    // Peak of the statistic over the next few positions.
    std::size_t peak = t;
    double peak_value = 0.0;
    for (std::size_t u = t; u < t + peak_window && u <= last; ++u) {
      const double d = coarse_metric(y, n, u, sps);
      if (d > peak_value) {
        peak_value = d;
        peak = u;
      }
    }

    const std::optional<PreambleEstimate> est = estimate_preamble(y, n, peak, config);
    if (est && est->verify_metric >= config.verify_threshold) return est;

    if (stats) {
      ++stats->rejected;
      if (est && est->verify_metric > stats->max_rejected_verify) {
        stats->max_rejected_verify = est->verify_metric;
      }
    }
    t = peak + static_cast<std::size_t>(sps);  // step past this lobe
  }
  return std::nullopt;
}

}  // namespace mpsk::dsp
