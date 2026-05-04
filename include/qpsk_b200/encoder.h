#ifndef QPSK_B200_ENCODER_H
#define QPSK_B200_ENCODER_H

#include <complex>
#include <cstdint>
#include <vector>

#include "qpsk_b200/fec.h"
#include "qpsk_b200/frame.h"
#include "qpsk_b200/pulse_shaper.h"
#include "qpsk_b200/symbol_mapper.h"
#include "qpsk_b200/types.h"

namespace qpsk_b200 {

/// Orchestrates the TX pipeline:
///   payload → FrameBuilder → (optional FEC) → SymbolMapper → PulseShaper → samples
///
/// When FEC is enabled, the frame is built with the original payload (so the
/// header contains the pre-FEC payload length and the CRC covers header +
/// original payload), then the payload bit section is replaced with FEC-coded
/// bits.  When FEC is disabled, the frame is transmitted as-is.
///
/// Maps to Requirements 3, 4, 6, 8.1, 9.1, 13.1, 13.5, 13.8.
class Encoder {
public:
    /// Construct an Encoder with the given configuration.
    explicit Encoder(const Config& config);

    /// Encode a byte payload into pulse-shaped complex samples.
    ///
    /// @param payload  Raw payload bytes to transmit.
    /// @return Complex baseband samples ready for the B200 TX streamer.
    std::vector<std::complex<float>> encode(const std::vector<uint8_t>& payload);

    /// Access cumulative TX diagnostics.
    const TxDiagnostics& diagnostics() const { return diag_; }

private:
    Config config_;
    FrameBuilder frame_builder_;
    FecEncoder fec_encoder_;
    PulseShaper shaper_;
    TxDiagnostics diag_;
};

} // namespace qpsk_b200

#endif // QPSK_B200_ENCODER_H
