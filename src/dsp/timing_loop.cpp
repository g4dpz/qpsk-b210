#include "dsp/timing_loop.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "dsp/farrow.hpp"
#include "dsp/fir.hpp"
#include "dsp/interpolator.hpp"
#include "dsp/rrc.hpp"

namespace mpsk::dsp {

double gardner_error(std::complex<float> current, std::complex<float> previous,
                     std::complex<float> midpoint) {
  const std::complex<double> c(static_cast<double>(current.real()), static_cast<double>(current.imag()));
  const std::complex<double> p(static_cast<double>(previous.real()), static_cast<double>(previous.imag()));
  const std::complex<double> m(static_cast<double>(midpoint.real()), static_cast<double>(midpoint.imag()));
  return std::real((c - p) * std::conj(m));
}

namespace {

/** A small, fixed linear congruential generator: reproducible on every platform. */
class Lcg {
 public:
  explicit Lcg(unsigned long long seed) : state_(seed) {}
  bool bit() {
    state_ = state_ * 6364136223846793005ull + 1442695040888963407ull;
    return (state_ >> 33) & 1u;
  }

 private:
  unsigned long long state_;
};

}  // namespace

double TimingLoop::measure_detector_gain(int samples_per_symbol, double rolloff, int span_symbols) {
  const int sps = samples_per_symbol;
  // The result depends on the particular random symbols, with a spread that falls as
  // 1/sqrt(N): measured standard deviation 0.056, 0.030, 0.017 and 0.0056 for 1000,
  // 3000, 10000 and 40000 symbols at roll-off 0.35. 20000 symbols gives about 1% and
  // takes around 15 ms.
  const int n_symbols = 20000;
  const float a = 0.70710678118654752f;

  Lcg rng(12345);
  std::vector<std::complex<float>> symbols(static_cast<std::size_t>(n_symbols));
  for (auto& s : symbols) s = {rng.bit() ? a : -a, rng.bit() ? a : -a};

  const std::vector<float> taps = rrc_taps(rolloff, sps, span_symbols);
  std::vector<std::complex<float>> tx = upsample_and_filter(symbols, taps, sps);
  tx.resize(tx.size() + taps.size(), std::complex<float>(0.0f, 0.0f));
  FirFilter matched(taps);
  const std::vector<std::complex<float>> y = matched.process(tx);

  // Both filters delay by span * sps / 2, so the first symbol peaks at span * sps.
  const double first_peak = static_cast<double>(span_symbols) * static_cast<double>(sps);

  const auto mean_error = [&](double offset_samples) {
    double sum = 0.0;
    int count = 0;
    for (int k = 60; k < n_symbols - 60; ++k) {
      const double t = first_peak + static_cast<double>(k) * sps + offset_samples;
      const auto cur = sinc_interpolate(y.data(), y.size(), t);
      const auto prev = sinc_interpolate(y.data(), y.size(), t - sps);
      const auto mid = sinc_interpolate(y.data(), y.size(), t - 0.5 * sps);
      sum += gardner_error(cur, prev, mid);
      ++count;
    }
    return sum / count;
  };

  const double h = 0.05 * sps;  // 5% of a symbol, in samples
  const double slope_per_sample = (mean_error(h) - mean_error(-h)) / (2.0 * h);
  return slope_per_sample * sps;  // per symbol
}

TimingLoop::TimingLoop(const TimingLoopConfig& config, double start_position)
    : sps_(config.samples_per_symbol),
      detector_gain_(0.0),
      position_(start_position),
      integrator_(0.0),
      step_(static_cast<double>(config.samples_per_symbol)) {
  if (config.samples_per_symbol < 2) {
    throw std::invalid_argument("TimingLoop: samples per symbol must be at least 2");
  }
  // The loop's variable is the position in SYMBOLS; the detector gain is per symbol,
  // so the gains below act on a position change measured in symbols. Converting to
  // samples is one multiplication by sps in update().
  detector_gain_ = (config.detector_gain > 0.0)
                       ? config.detector_gain
                       : measure_detector_gain(config.samples_per_symbol, config.rolloff,
                                               config.span_symbols);
  gains_ = second_order_gains(config.bn_t, config.damping, detector_gain_);
}

void TimingLoop::update(double error) {
  // Late sampling gives a positive detector error, and the loop must move the next
  // sample earlier, so the control signal is the negative of the error. The update is
  // then the structure in loop_gains.hpp, with the position measured in symbols:
  //
  //   I += Ki * c;   v = Kp * c + I;   next position = this position + sps * (1 + v)
  //
  // I is a fractional change of the symbol period, so it is the clock offset the loop
  // has learned, and sps * (1 + I) is the symbol period used for the midpoint.
  const double control = -error;
  integrator_ += gains_.ki * control;
  const double v = gains_.kp * control + integrator_;
  step_ = static_cast<double>(sps_) * (1.0 + integrator_);
  position_ += static_cast<double>(sps_) * (1.0 + v);
}

double TimingLoop::clock_offset_ppm() const { return integrator_ * 1e6; }

}  // namespace mpsk::dsp
