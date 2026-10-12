#include "dsp/rx_frontend.hpp"

#include "dsp/rrc.hpp"

namespace mpsk::dsp {

RxFrontEnd::RxFrontEnd(const RxFrontEndConfig& config)
    : config_(config),
      dc_blocker_(config.dc_pole),
      filter_(rrc_taps(config.rolloff, config.samples_per_symbol, config.span_symbols)),
      group_delay_(rrc_group_delay(config.samples_per_symbol, config.span_symbols)) {}

void RxFrontEnd::process(const std::complex<float>* in, std::complex<float>* out, std::size_t n) {
  // The DC blocker works on the whole block so that, right after a reset, it can
  // prime itself from the mean of the block's first samples (see dc_blocker.hpp).
  dc_blocker_.process(in, out, n);
  filter_.process(out, out, n);
  samples_processed_ += n;
}

std::vector<std::complex<float>> RxFrontEnd::process(const std::vector<std::complex<float>>& in) {
  std::vector<std::complex<float>> out(in.size());
  process(in.data(), out.data(), in.size());
  return out;
}

void RxFrontEnd::reset() {
  dc_blocker_.reset();
  filter_.reset();
  samples_processed_ = 0;
}

}  // namespace mpsk::dsp
