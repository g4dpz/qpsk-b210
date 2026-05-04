#include "qpsk_b200/types.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

namespace qpsk_b200 {

// Helper: convert CodeRate to string for JSON serialization
static std::string code_rate_to_string(CodeRate rate) {
    switch (rate) {
        case CodeRate::RATE_1_2: return "1/2";
        case CodeRate::RATE_3_4: return "3/4";
        default:
            throw std::invalid_argument(
                "Unsupported FEC code rate for serialization");
    }
}

// Helper: convert string to CodeRate for JSON deserialization
static CodeRate string_to_code_rate(const std::string& s) {
    if (s == "1/2") return CodeRate::RATE_1_2;
    if (s == "3/4") return CodeRate::RATE_3_4;
    throw std::invalid_argument(
        "Invalid fec_code_rate value '" + s +
        "'. Supported values: \"1/2\", \"3/4\"");
}

Config Config::defaults() {
    return Config{};
}

void Config::validate() const {
    // center_freq: 70 MHz – 6 GHz
    if (center_freq < 70e6 || center_freq > 6e9) {
        std::ostringstream oss;
        oss << "center_freq " << static_cast<long long>(center_freq)
            << " Hz is outside valid range [70000000, 6000000000] Hz";
        throw std::invalid_argument(oss.str());
    }

    // sample_rate: > 0 and <= 56 MSPS
    if (sample_rate <= 0.0 || sample_rate > 56e6) {
        std::ostringstream oss;
        oss << "sample_rate " << sample_rate
            << " Hz is outside valid range (0, 56000000] Hz";
        throw std::invalid_argument(oss.str());
    }

    // tx_gain: 0 – 89.75 dB
    if (tx_gain < 0.0 || tx_gain > 89.75) {
        std::ostringstream oss;
        oss << "tx_gain " << tx_gain
            << " dB is outside valid range [0, 89.75] dB";
        throw std::invalid_argument(oss.str());
    }

    // rx_gain: 0 – 76 dB
    if (rx_gain < 0.0 || rx_gain > 76.0) {
        std::ostringstream oss;
        oss << "rx_gain " << rx_gain
            << " dB is outside valid range [0, 76] dB";
        throw std::invalid_argument(oss.str());
    }

    // samples_per_symbol: >= 2
    if (samples_per_symbol < 2) {
        std::ostringstream oss;
        oss << "samples_per_symbol " << samples_per_symbol
            << " is below minimum value of 2";
        throw std::invalid_argument(oss.str());
    }

    // rrc_rolloff: > 0.0 and <= 1.0
    if (rrc_rolloff <= 0.0 || rrc_rolloff > 1.0) {
        std::ostringstream oss;
        oss << "rrc_rolloff " << rrc_rolloff
            << " is outside valid range (0, 1.0]";
        throw std::invalid_argument(oss.str());
    }

    // rrc_span_symbols: >= 1
    if (rrc_span_symbols < 1) {
        std::ostringstream oss;
        oss << "rrc_span_symbols " << rrc_span_symbols
            << " is below minimum value of 1";
        throw std::invalid_argument(oss.str());
    }

    // fec_code_rate: must be RATE_1_2 or RATE_3_4
    auto rate_val = static_cast<uint8_t>(fec_code_rate);
    if (rate_val != static_cast<uint8_t>(CodeRate::RATE_1_2) &&
        rate_val != static_cast<uint8_t>(CodeRate::RATE_3_4)) {
        std::ostringstream oss;
        oss << "fec_code_rate " << static_cast<int>(rate_val)
            << " is not a supported value. Supported values: 1/2, 3/4";
        throw std::invalid_argument(oss.str());
    }

    // tcp_input_port: > 0
    if (tcp_input_port == 0) {
        throw std::invalid_argument(
            "tcp_input_port 0 is invalid; port must be > 0");
    }

    // tcp_output_port: > 0
    if (tcp_output_port == 0) {
        throw std::invalid_argument(
            "tcp_output_port 0 is invalid; port must be > 0");
    }

    // preamble: must not be empty
    if (preamble.empty()) {
        throw std::invalid_argument(
            "preamble must not be empty");
    }
}

void Config::to_json(const std::string& path) const {
    nlohmann::json j;

    j["center_freq"]       = center_freq;
    j["sample_rate"]       = sample_rate;
    j["tx_gain"]           = tx_gain;
    j["rx_gain"]           = rx_gain;
    j["tx_antenna"]        = tx_antenna;
    j["rx_antenna"]        = rx_antenna;
    j["samples_per_symbol"] = samples_per_symbol;
    j["rrc_rolloff"]       = rrc_rolloff;
    j["rrc_span_symbols"]  = rrc_span_symbols;
    j["preamble"]          = preamble;
    j["tcp_input_addr"]    = tcp_input_addr;
    j["tcp_input_port"]    = tcp_input_port;
    j["tcp_output_addr"]   = tcp_output_addr;
    j["tcp_output_port"]   = tcp_output_port;
    j["fec_enabled"]       = fec_enabled;
    j["fec_code_rate"]     = code_rate_to_string(fec_code_rate);

    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        throw std::invalid_argument(
            "Failed to open file for writing: " + path);
    }
    ofs << j.dump(4);
    if (!ofs.good()) {
        throw std::invalid_argument(
            "Failed to write JSON to file: " + path);
    }
}

Config Config::from_json(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::invalid_argument(
            "Failed to open configuration file: " + path);
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::invalid_argument(
            std::string("Failed to parse JSON from file: ") + e.what());
    }

    if (!j.is_object()) {
        throw std::invalid_argument(
            "Configuration JSON must be an object");
    }

    // Helper lambda: check that a required field exists
    auto require_field = [&](const std::string& field) {
        if (!j.contains(field)) {
            throw std::invalid_argument(
                "Missing required field '" + field +
                "' in configuration JSON");
        }
    };

    // Helper lambda: check field type and throw descriptive error
    auto check_type = [&](const std::string& field,
                          const std::string& expected_type,
                          auto predicate) {
        if (!predicate(j[field])) {
            throw std::invalid_argument(
                "Field '" + field + "' must be " + expected_type +
                ", got " + std::string(j[field].type_name()));
        }
    };

    Config cfg;

    // center_freq
    require_field("center_freq");
    check_type("center_freq", "a number",
               [](const nlohmann::json& v) { return v.is_number(); });
    cfg.center_freq = j["center_freq"].get<double>();

    // sample_rate
    require_field("sample_rate");
    check_type("sample_rate", "a number",
               [](const nlohmann::json& v) { return v.is_number(); });
    cfg.sample_rate = j["sample_rate"].get<double>();

    // tx_gain
    require_field("tx_gain");
    check_type("tx_gain", "a number",
               [](const nlohmann::json& v) { return v.is_number(); });
    cfg.tx_gain = j["tx_gain"].get<double>();

    // rx_gain
    require_field("rx_gain");
    check_type("rx_gain", "a number",
               [](const nlohmann::json& v) { return v.is_number(); });
    cfg.rx_gain = j["rx_gain"].get<double>();

    // tx_antenna
    require_field("tx_antenna");
    check_type("tx_antenna", "a string",
               [](const nlohmann::json& v) { return v.is_string(); });
    cfg.tx_antenna = j["tx_antenna"].get<std::string>();

    // rx_antenna
    require_field("rx_antenna");
    check_type("rx_antenna", "a string",
               [](const nlohmann::json& v) { return v.is_string(); });
    cfg.rx_antenna = j["rx_antenna"].get<std::string>();

    // samples_per_symbol
    require_field("samples_per_symbol");
    check_type("samples_per_symbol", "an integer",
               [](const nlohmann::json& v) { return v.is_number_integer(); });
    cfg.samples_per_symbol = j["samples_per_symbol"].get<int>();

    // rrc_rolloff
    require_field("rrc_rolloff");
    check_type("rrc_rolloff", "a number",
               [](const nlohmann::json& v) { return v.is_number(); });
    cfg.rrc_rolloff = j["rrc_rolloff"].get<double>();

    // rrc_span_symbols
    require_field("rrc_span_symbols");
    check_type("rrc_span_symbols", "an integer",
               [](const nlohmann::json& v) { return v.is_number_integer(); });
    cfg.rrc_span_symbols = j["rrc_span_symbols"].get<int>();

    // preamble
    require_field("preamble");
    check_type("preamble", "an array",
               [](const nlohmann::json& v) { return v.is_array(); });
    cfg.preamble = j["preamble"].get<std::vector<uint8_t>>();

    // tcp_input_addr
    require_field("tcp_input_addr");
    check_type("tcp_input_addr", "a string",
               [](const nlohmann::json& v) { return v.is_string(); });
    cfg.tcp_input_addr = j["tcp_input_addr"].get<std::string>();

    // tcp_input_port
    require_field("tcp_input_port");
    check_type("tcp_input_port", "an integer",
               [](const nlohmann::json& v) { return v.is_number_integer(); });
    cfg.tcp_input_port = j["tcp_input_port"].get<uint16_t>();

    // tcp_output_addr
    require_field("tcp_output_addr");
    check_type("tcp_output_addr", "a string",
               [](const nlohmann::json& v) { return v.is_string(); });
    cfg.tcp_output_addr = j["tcp_output_addr"].get<std::string>();

    // tcp_output_port
    require_field("tcp_output_port");
    check_type("tcp_output_port", "an integer",
               [](const nlohmann::json& v) { return v.is_number_integer(); });
    cfg.tcp_output_port = j["tcp_output_port"].get<uint16_t>();

    // fec_enabled
    require_field("fec_enabled");
    check_type("fec_enabled", "a boolean",
               [](const nlohmann::json& v) { return v.is_boolean(); });
    cfg.fec_enabled = j["fec_enabled"].get<bool>();

    // fec_code_rate (serialized as string "1/2" or "3/4")
    require_field("fec_code_rate");
    check_type("fec_code_rate", "a string",
               [](const nlohmann::json& v) { return v.is_string(); });
    cfg.fec_code_rate = string_to_code_rate(
        j["fec_code_rate"].get<std::string>());

    // Validate the loaded configuration
    cfg.validate();

    return cfg;
}

} // namespace qpsk_b200
