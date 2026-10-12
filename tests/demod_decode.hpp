// Shared by test_demodulator and test_demodulator_noise: run acquisition and the
// demodulator on a scenario and score the payload against the PRBS it carried.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp/demodulator.hpp"
#include "dsp/metrics.hpp"
#include "dsp/preamble_detector.hpp"
#include "dsp/prbs.hpp"
#include "rx_scenario.hpp"

namespace rx_test {

using namespace mpsk::dsp;

constexpr std::uint32_t kSeed = 3;   // the PRBS seed rx_scenario.hpp uses

struct Decoded {
  bool found = false;
  PreambleEstimate est;
  DemodResult result;
  BerResult ber;
};

/** Runs acquisition then the demodulator on a scenario, and scores the payload against the PRBS. */
inline Decoded decode(const Scenario& s, std::uint16_t counter = 1, std::size_t payload_bytes = 128,
               const DemodConfig& dc = DemodConfig{}) {
  Decoded d;
  const Received r = make_received(s, counter, payload_bytes);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  if (!e) return d;
  d.found = true;
  d.est = *e;
  Demodulator demod(dc);
  d.result = demod.demodulate(r.y.data(), r.y.size(), *e);
  d.ber = payload_ber(d.result.frame.payload, prbs_bytes(kSeed, counter, payload_bytes));
  return d;
}


}  // namespace rx_test
