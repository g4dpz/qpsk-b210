#ifndef QPSK_B200_DECODER_H
#define QPSK_B200_DECODER_H

#include <complex>
#include <cstdint>
#include <optional>
#include <vector>

#include "qpsk_b200/carrier_sync.h"
#include "qpsk_b200/fec.h"
#include "qpsk_b200/frame.h"
#include "qpsk_b200/frame_sync.h"
#include "qpsk_b200/pulse_shaper.h"
#include "qpsk_b200/symbol_mapper.h"
#include "qpsk_b200/timing_recovery.h"
#include "qpsk_b200/types.h"

namespace qpsk_b200 {

/// Orchestrates the RX pipeline:
///   samples → matched filter → carrier sync → timing recovery →
///   frame sync → demap → (optional FEC decode) → frame parse → payload
///
/// When FEC is enabled (per the frame header), the coded payload bytes are
/// FEC-decoded using the Viterbi algorithm before the final payload is
/// returned.  When FEC is disabled, the payload is returned directly from
/// the parsed frame.
///
/// Maps to Requirements 5, 6, 7, 8.2, 9.2, 9.3, 9.4, 9.5, 13.2, 13.8, 13.10.
class Decoder {
public:
    /// Construct a Decoder with the given configuration.
    explicit Decoder(const Config& config);

    /// Decode complex baseband samples into a payload byte vector.
    ///
    /// @param samples  Complex baseband samples from the B200 RX streamer
    ///                 (or loopback from Encoder).
    /// @return Decoded payload bytes, or std::nullopt if no valid frame
    ///         was found or CRC verification failed.
    std::optional<std::vector<uint8_t>>
    decode(const std::vector<std::complex<float>>& samples);

    /// Access cumulative RX diagnostics.
    const RxDiagnostics& diagnostics() const { return diag_; }

    /// Reset all internal DSP state (carrier sync, timing recovery, etc.).
    void reset();

private:
    Config config_;
    PulseShaper shaper_;
    CarrierSynchronizer carrier_sync_;
    TimingRecovery timing_recovery_;
    FrameSynchronizer frame_sync_;
    FecDecoder fec_decoder_;
    RxDiagnostics diag_;
};

} // namespace qpsk_b200

#endif // QPSK_B200_DECODER_H
