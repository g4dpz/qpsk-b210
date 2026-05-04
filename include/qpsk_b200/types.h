#ifndef QPSK_B200_TYPES_H
#define QPSK_B200_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace qpsk_b200 {

// FEC code rate enumeration
enum class CodeRate : uint8_t {
    RATE_1_2 = 0,  // 1/2 — doubles payload length
    RATE_3_4 = 1   // 3/4 — 4/3 payload length
};

// Runtime configuration
struct Config {
    // RF parameters
    double center_freq    = 2.4e9;   // Hz
    double sample_rate    = 1e6;     // samples/sec
    double tx_gain        = 40.0;    // dB
    double rx_gain        = 40.0;    // dB
    std::string tx_antenna = "TX/RX";
    std::string rx_antenna = "RX2";

    // DSP parameters
    int    samples_per_symbol = 4;
    double rrc_rolloff        = 0.35;
    int    rrc_span_symbols   = 6;     // filter spans 6 symbols
    std::vector<uint8_t> preamble = {1,1,1,1,1,0,0,1,1,0,1,0,1};

    // TCP parameters
    std::string tcp_input_addr  = "127.0.0.1";
    uint16_t    tcp_input_port  = 5000;
    std::string tcp_output_addr = "127.0.0.1";
    uint16_t    tcp_output_port = 5001;

    // FEC parameters
    bool     fec_enabled  = true;
    CodeRate fec_code_rate = CodeRate::RATE_1_2;

    // Validation — throws std::invalid_argument on invalid params
    void validate() const;

    // JSON serialization
    void to_json(const std::string& path) const;
    static Config from_json(const std::string& path);

    // Factory defaults
    static Config defaults();
};

// Frame header (parsed from bit stream)
struct FrameHeader {
    uint16_t payload_length  = 0;   // bytes
    uint8_t  padding_bits    = 0;   // 0–7
    uint16_t sequence_number = 0;   // 0–4095
    bool     fec_enabled     = false;
    CodeRate fec_code_rate   = CodeRate::RATE_1_2;
};

// Complete parsed frame
struct Frame {
    FrameHeader header;
    std::vector<uint8_t> payload;
    uint32_t crc       = 0;     // received CRC
    bool     crc_valid = false; // computed == received
};

// Diagnostic counters — transmit side
struct TxDiagnostics {
    uint64_t frames_transmitted  = 0;
    uint64_t symbols_transmitted = 0;
};

// Diagnostic counters — receive side
struct RxDiagnostics {
    uint64_t frames_received      = 0;
    uint64_t crc_errors           = 0;
    double   estimated_ber        = 0.0;
    double   last_freq_offset     = 0.0;
    double   last_phase_offset    = 0.0;
    double   last_timing_offset   = 0.0;
    uint64_t fec_errors_corrected = 0;
};

// Diagnostic counters — TCP servers
struct TcpDiagnostics {
    uint64_t clients_connected    = 0;
    uint64_t clients_disconnected = 0;
    uint64_t bytes_received       = 0;   // input server
    uint64_t bytes_sent           = 0;   // output server
};

} // namespace qpsk_b200

#endif // QPSK_B200_TYPES_H
