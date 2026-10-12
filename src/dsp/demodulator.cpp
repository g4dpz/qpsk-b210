#include "dsp/demodulator.hpp"

#include <cmath>
#include <stdexcept>

#include "dsp/constellation.hpp"
#include "dsp/farrow.hpp"
#include "dsp/loop_gains.hpp"
#include "dsp/metrics.hpp"
#include "dsp/preamble.hpp"

namespace mpsk::dsp {

namespace {

using cf = std::complex<float>;

/**
 * Runs the two loops over @p count symbols with one constellation, appending the
 * corrected symbols to @p out. Stops early, and returns false, if the buffer runs out.
 *
 * @param evm        If not null, every symbol is added to it.
 * @param omega_sum  If not null, the carrier frequency after each symbol is added.
 * @param ppm_sum    If not null, the timing loop's clock-offset reading after each symbol
 *                   is added. The caller averages: a single reading is noisy.
 */
bool run_symbols(const cf* y, std::size_t n, double inv_amplitude, const Constellation& constellation,
                 std::size_t count, CarrierLoop& carrier, TimingLoop& timing,
                 std::vector<cf>& out, EvmAccumulator* evm, double* omega_sum, double* ppm_sum) {
  for (std::size_t i = 0; i < count; ++i) {
    const double t_cur = timing.next_position();
    const double t_mid = timing.midpoint_position();
    const double t_prev = timing.previous_position();
    if (!farrow_in_range(n, t_cur) || !farrow_in_range(n, t_mid) || !farrow_in_range(n, t_prev)) {
      return false;
    }

    const float scale = static_cast<float>(inv_amplitude);
    const cf current = carrier.derotate(farrow_cubic(y, n, t_cur) * scale, 0.0);
    const cf midpoint = carrier.derotate(farrow_cubic(y, n, t_mid) * scale, 0.5);
    const cf previous = carrier.derotate(farrow_cubic(y, n, t_prev) * scale, 1.0);

    const cf decision = constellation.slice(current).point;
    const double timing_error = gardner_error(current, previous, midpoint);

    if (evm) evm->add(current, decision);
    carrier.update(current, decision);
    timing.update(timing_error);
    if (omega_sum) *omega_sum += carrier.frequency();
    if (ppm_sum) *ppm_sum += timing.clock_offset_ppm();
    out.push_back(current);
  }
  return true;
}

}  // namespace

Demodulator::Demodulator(const DemodConfig& config) : config_(config), detector_gain_(0.0) {
  if (config.samples_per_symbol < 2) {
    throw std::invalid_argument("Demodulator: samples per symbol must be at least 2");
  }
  // Validate the loop parameters now, not on the first frame.
  (void)second_order_gains(config.carrier.bn_t, config.carrier.damping, 1.0);
  (void)second_order_gains(config.timing_bn_t, config.timing_damping, 1.0);

  // Measured once here, because it takes a few milliseconds and every frame needs it.
  detector_gain_ = (config.timing_detector_gain > 0.0)
                       ? config.timing_detector_gain
                       : TimingLoop::measure_detector_gain(config.samples_per_symbol, config.rolloff,
                                                           config.span_symbols);
}

DemodResult Demodulator::demodulate(const cf* y, std::size_t n, const PreambleEstimate& est) const {
  DemodResult result;
  DecodedFrame& frame = result.frame;
  const int sps = config_.samples_per_symbol;

  if (!(est.amplitude > 0.0) || !std::isfinite(est.amplitude)) {
    frame.error = "the preamble amplitude estimate is not usable";
    return result;
  }
  const double inv_amplitude = 1.0 / est.amplitude;

  // The loops start at the first header symbol (index 64 from the start of the frame).
  // The carrier phase there is extrapolated from the preamble centre; the sampling
  // position is the preamble's, advanced by 64 symbols.
  const double first_header = static_cast<double>(kPreambleSymbols);
  CarrierLoop carrier(config_.carrier, est.phase_at(first_header), est.omega);

  TimingLoopConfig tc;
  tc.samples_per_symbol = sps;
  tc.rolloff = config_.rolloff;
  tc.span_symbols = config_.span_symbols;
  tc.bn_t = config_.timing_bn_t;
  tc.damping = config_.timing_damping;
  tc.detector_gain = detector_gain_;
  TimingLoop timing(tc, est.position + first_header * static_cast<double>(sps));

  // ---- Header: BPSK ---------------------------------------------------------
  const Constellation bpsk(2);
  double header_omega_sum = 0.0;
  double header_ppm_sum = 0.0;
  result.header_symbols.reserve(static_cast<std::size_t>(kHeaderSymbols));
  if (!run_symbols(y, n, inv_amplitude, bpsk, static_cast<std::size_t>(kHeaderSymbols), carrier, timing,
                   result.header_symbols, nullptr, &header_omega_sum, &header_ppm_sum)) {
    result.truncated = true;
    result.symbols_decoded = result.header_symbols.size();
    frame.error = "the buffer ends inside the header";
    result.omega = est.omega;
    result.clock_offset_ppm = 0.0;   // too few symbols for a reading worth reporting
    return result;
  }
  result.symbols_decoded = result.header_symbols.size();

  const HeaderResult header = parse_header_bits(demap_symbols(bpsk, result.header_symbols));
  if (!header.ok) {
    // A frame that cannot be identified is lost. Nothing after the header is trusted.
    frame.error = header.error;
    result.omega = header_omega_sum / static_cast<double>(kHeaderSymbols);
    result.clock_offset_ppm = header_ppm_sum / static_cast<double>(kHeaderSymbols);
    return result;
  }
  frame.header_ok = true;
  frame.header = header.header;

  // ---- Payload section: the frame's own modulation --------------------------
  const Constellation payload_constellation(header.header.modulation_order);
  const std::size_t payload_symbols = payload_section_symbols(
      header.header.type, payload_constellation.bits_per_symbol(), header.header.payload_length);

  EvmAccumulator evm;
  double payload_omega_sum = 0.0;
  double payload_ppm_sum = 0.0;
  result.payload_symbols.reserve(payload_symbols);
  const bool complete = run_symbols(y, n, inv_amplitude, payload_constellation, payload_symbols, carrier,
                                    timing, result.payload_symbols, &evm, &payload_omega_sum, &payload_ppm_sum);
  result.symbols_decoded += result.payload_symbols.size();
  result.evm = evm.rms();
  result.evm_snr_db = evm.snr_db();
  result.evm_symbols = evm.count();

  // Both loop readings are averaged over the payload. A single reading is noisy: on a
  // signal with no drift at all the timing loop's instantaneous clock-offset reading
  // wanders by about 8 ppm rms at the default bandwidth (see timing_loop.hpp).
  if (!result.payload_symbols.empty()) {
    const double count = static_cast<double>(result.payload_symbols.size());
    result.omega = payload_omega_sum / count;
    result.clock_offset_ppm = payload_ppm_sum / count;
  } else {
    result.omega = header_omega_sum / static_cast<double>(kHeaderSymbols);
    result.clock_offset_ppm = header_ppm_sum / static_cast<double>(kHeaderSymbols);
  }

  if (!complete) {
    result.truncated = true;
    frame.error = "frame is truncated: " + std::to_string(result.payload_symbols.size()) +
                  " payload symbols read, " + std::to_string(payload_symbols) + " needed";
    return result;
  }

  const PayloadSection section = unpack_payload_section(
      header.header.type, header.header.payload_length,
      demap_symbols(payload_constellation, result.payload_symbols));
  frame.payload_crc_ok = section.crc_ok;
  frame.payload = section.payload;
  frame.trailer = section.trailer;
  return result;
}

}  // namespace mpsk::dsp
