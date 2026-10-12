#include "dsp/loop_gains.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace mpsk::dsp {

LoopGains second_order_gains(double bn_t, double zeta, double detector_gain) {
  if (!(bn_t >= 0.0 && bn_t < 0.5)) {  // also rejects NaN
    throw std::invalid_argument("second_order_gains: Bn*T must be in [0, 0.5), got " +
                                std::to_string(bn_t));
  }
  if (!(zeta > 0.0)) {
    throw std::invalid_argument("second_order_gains: damping must be positive, got " +
                                std::to_string(zeta));
  }
  if (!(detector_gain > 0.0)) {
    throw std::invalid_argument("second_order_gains: detector gain must be positive, got " +
                                std::to_string(detector_gain));
  }

  const double theta = bn_t / (zeta + 1.0 / (4.0 * zeta));
  const double d = 1.0 + 2.0 * zeta * theta + theta * theta;
  LoopGains g;
  g.kp = 4.0 * zeta * theta / d / detector_gain;
  g.ki = 4.0 * theta * theta / d / detector_gain;
  return g;
}

}  // namespace mpsk::dsp
