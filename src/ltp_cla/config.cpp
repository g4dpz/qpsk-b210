#include "ltp_cla/config.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ltp {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

bool LtpClaConfig::operator==(const LtpClaConfig& o) const {
    return ltp.local_engine_id == o.ltp.local_engine_id &&
           ltp.remote_engine_id == o.ltp.remote_engine_id &&
           ltp.max_segment_size == o.ltp.max_segment_size &&
           ltp.retransmission_timeout_ms == o.ltp.retransmission_timeout_ms &&
           ltp.max_retransmissions == o.ltp.max_retransmissions &&
           ltp.max_concurrent_sessions == o.ltp.max_concurrent_sessions &&
           cla.tx_addr == o.cla.tx_addr &&
           cla.tx_port == o.cla.tx_port &&
           cla.rx_addr == o.cla.rx_addr &&
           cla.rx_port == o.cla.rx_port &&
           cla.initial_reconnect_delay_ms == o.cla.initial_reconnect_delay_ms &&
           cla.max_reconnect_delay_ms == o.cla.max_reconnect_delay_ms &&
           ingress.bind_addr == o.ingress.bind_addr &&
           ingress.port == o.ingress.port &&
           ingress.default_reliable == o.ingress.default_reliable &&
           egress.bind_addr == o.egress.bind_addr &&
           egress.port == o.egress.port &&
           egress.max_buffer_bytes == o.egress.max_buffer_bytes &&
           log_level == o.log_level;
}

// ---------------------------------------------------------------------------
// defaults
// ---------------------------------------------------------------------------

LtpClaConfig LtpClaConfig::defaults() {
    return LtpClaConfig{};
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

std::vector<std::string> LtpClaConfig::validate() const {
    std::vector<std::string> errors;

    if (ltp.local_engine_id == 0)
        errors.push_back("ltp.local_engine_id must be > 0, got 0");
    if (ltp.remote_engine_id == 0)
        errors.push_back("ltp.remote_engine_id must be > 0, got 0");
    if (ltp.max_segment_size == 0)
        errors.push_back("ltp.max_segment_size must be > 0, got 0");
    if (ltp.retransmission_timeout_ms < 100)
        errors.push_back("ltp.retransmission_timeout_ms must be >= 100, got " +
                         std::to_string(ltp.retransmission_timeout_ms));
    if (ltp.max_retransmissions == 0)
        errors.push_back("ltp.max_retransmissions must be > 0, got 0");
    if (ltp.max_concurrent_sessions == 0)
        errors.push_back("ltp.max_concurrent_sessions must be > 0, got 0");

    if (cla.tx_addr.empty())
        errors.push_back("cla.tx_addr must not be empty");
    if (cla.tx_port == 0)
        errors.push_back("cla.tx_port must be > 0, got 0");
    if (cla.rx_addr.empty())
        errors.push_back("cla.rx_addr must not be empty");
    if (cla.rx_port == 0)
        errors.push_back("cla.rx_port must be > 0, got 0");
    if (cla.initial_reconnect_delay_ms == 0)
        errors.push_back("cla.initial_reconnect_delay_ms must be > 0, got 0");
    if (cla.max_reconnect_delay_ms < cla.initial_reconnect_delay_ms)
        errors.push_back("cla.max_reconnect_delay_ms must be >= initial_reconnect_delay_ms");

    if (ingress.bind_addr.empty())
        errors.push_back("ingress.bind_addr must not be empty");

    if (egress.bind_addr.empty())
        errors.push_back("egress.bind_addr must not be empty");
    if (egress.max_buffer_bytes == 0)
        errors.push_back("egress.max_buffer_bytes must be > 0, got 0");

    if (log_level != "trace" && log_level != "debug" && log_level != "info" &&
        log_level != "warn" && log_level != "error" && log_level != "critical" &&
        log_level != "off")
        errors.push_back("log_level must be one of: trace, debug, info, warn, "
                         "error, critical, off; got '" + log_level + "'");

    return errors;
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

std::string LtpClaConfig::to_json() const {
    json j;

    j["ltp"]["local_engine_id"] = ltp.local_engine_id;
    j["ltp"]["remote_engine_id"] = ltp.remote_engine_id;
    j["ltp"]["max_segment_size"] = ltp.max_segment_size;
    j["ltp"]["retransmission_timeout_ms"] = ltp.retransmission_timeout_ms;
    j["ltp"]["max_retransmissions"] = ltp.max_retransmissions;
    j["ltp"]["max_concurrent_sessions"] = ltp.max_concurrent_sessions;

    j["cla"]["tx_addr"] = cla.tx_addr;
    j["cla"]["tx_port"] = cla.tx_port;
    j["cla"]["rx_addr"] = cla.rx_addr;
    j["cla"]["rx_port"] = cla.rx_port;
    j["cla"]["initial_reconnect_delay_ms"] = cla.initial_reconnect_delay_ms;
    j["cla"]["max_reconnect_delay_ms"] = cla.max_reconnect_delay_ms;

    j["ingress"]["bind_addr"] = ingress.bind_addr;
    j["ingress"]["port"] = ingress.port;
    j["ingress"]["default_reliable"] = ingress.default_reliable;

    j["egress"]["bind_addr"] = egress.bind_addr;
    j["egress"]["port"] = egress.port;
    j["egress"]["max_buffer_bytes"] = egress.max_buffer_bytes;

    j["log_level"] = log_level;

    return j.dump(2);
}

void LtpClaConfig::to_json_file(const std::string& path) const {
    std::ofstream ofs(path);
    if (!ofs) throw std::runtime_error("Cannot open file for writing: " + path);
    ofs << to_json();
}

// ---------------------------------------------------------------------------
// JSON deserialization
// ---------------------------------------------------------------------------

LtpClaConfig LtpClaConfig::from_json(const std::string& json_str) {
    auto j = json::parse(json_str);
    LtpClaConfig cfg;

    if (j.contains("ltp")) {
        auto& jl = j["ltp"];
        if (jl.contains("local_engine_id"))
            cfg.ltp.local_engine_id = jl["local_engine_id"].get<uint64_t>();
        if (jl.contains("remote_engine_id"))
            cfg.ltp.remote_engine_id = jl["remote_engine_id"].get<uint64_t>();
        if (jl.contains("max_segment_size"))
            cfg.ltp.max_segment_size = jl["max_segment_size"].get<uint32_t>();
        if (jl.contains("retransmission_timeout_ms"))
            cfg.ltp.retransmission_timeout_ms = jl["retransmission_timeout_ms"].get<uint32_t>();
        if (jl.contains("max_retransmissions"))
            cfg.ltp.max_retransmissions = jl["max_retransmissions"].get<uint8_t>();
        if (jl.contains("max_concurrent_sessions"))
            cfg.ltp.max_concurrent_sessions = jl["max_concurrent_sessions"].get<uint32_t>();
    }

    if (j.contains("cla")) {
        auto& jc = j["cla"];
        if (jc.contains("tx_addr")) cfg.cla.tx_addr = jc["tx_addr"].get<std::string>();
        if (jc.contains("tx_port")) cfg.cla.tx_port = jc["tx_port"].get<uint16_t>();
        if (jc.contains("rx_addr")) cfg.cla.rx_addr = jc["rx_addr"].get<std::string>();
        if (jc.contains("rx_port")) cfg.cla.rx_port = jc["rx_port"].get<uint16_t>();
        if (jc.contains("initial_reconnect_delay_ms"))
            cfg.cla.initial_reconnect_delay_ms = jc["initial_reconnect_delay_ms"].get<uint32_t>();
        if (jc.contains("max_reconnect_delay_ms"))
            cfg.cla.max_reconnect_delay_ms = jc["max_reconnect_delay_ms"].get<uint32_t>();
    }

    if (j.contains("ingress")) {
        auto& ji = j["ingress"];
        if (ji.contains("bind_addr")) cfg.ingress.bind_addr = ji["bind_addr"].get<std::string>();
        if (ji.contains("port")) cfg.ingress.port = ji["port"].get<uint16_t>();
        if (ji.contains("default_reliable"))
            cfg.ingress.default_reliable = ji["default_reliable"].get<bool>();
    }

    if (j.contains("egress")) {
        auto& je = j["egress"];
        if (je.contains("bind_addr")) cfg.egress.bind_addr = je["bind_addr"].get<std::string>();
        if (je.contains("port")) cfg.egress.port = je["port"].get<uint16_t>();
        if (je.contains("max_buffer_bytes"))
            cfg.egress.max_buffer_bytes = je["max_buffer_bytes"].get<uint64_t>();
    }

    if (j.contains("log_level"))
        cfg.log_level = j["log_level"].get<std::string>();

    return cfg;
}

LtpClaConfig LtpClaConfig::from_json_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) throw std::runtime_error("Cannot open config file: " + path);
    std::stringstream ss;
    ss << ifs.rdbuf();
    return from_json(ss.str());
}

// ---------------------------------------------------------------------------
// CLI overrides
// ---------------------------------------------------------------------------

void LtpClaConfig::apply_cli_overrides(int argc, const char* const* argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        auto eq = arg.find('=');
        if (eq == std::string::npos || arg.substr(0, 2) != "--") continue;

        std::string key = arg.substr(2, eq - 2);
        std::string val = arg.substr(eq + 1);

        try {
            if (key == "ltp.local_engine_id")
                ltp.local_engine_id = std::stoull(val);
            else if (key == "ltp.remote_engine_id")
                ltp.remote_engine_id = std::stoull(val);
            else if (key == "ltp.max_segment_size")
                ltp.max_segment_size = static_cast<uint32_t>(std::stoul(val));
            else if (key == "ltp.retransmission_timeout_ms")
                ltp.retransmission_timeout_ms = static_cast<uint32_t>(std::stoul(val));
            else if (key == "ltp.max_retransmissions")
                ltp.max_retransmissions = static_cast<uint8_t>(std::stoul(val));
            else if (key == "ltp.max_concurrent_sessions")
                ltp.max_concurrent_sessions = static_cast<uint32_t>(std::stoul(val));
            else if (key == "cla.tx_addr")
                cla.tx_addr = val;
            else if (key == "cla.tx_port")
                cla.tx_port = static_cast<uint16_t>(std::stoul(val));
            else if (key == "cla.rx_addr")
                cla.rx_addr = val;
            else if (key == "cla.rx_port")
                cla.rx_port = static_cast<uint16_t>(std::stoul(val));
            else if (key == "cla.initial_reconnect_delay_ms")
                cla.initial_reconnect_delay_ms = static_cast<uint32_t>(std::stoul(val));
            else if (key == "cla.max_reconnect_delay_ms")
                cla.max_reconnect_delay_ms = static_cast<uint32_t>(std::stoul(val));
            else if (key == "ingress.bind_addr")
                ingress.bind_addr = val;
            else if (key == "ingress.port")
                ingress.port = static_cast<uint16_t>(std::stoul(val));
            else if (key == "ingress.default_reliable")
                ingress.default_reliable = (val == "true" || val == "1");
            else if (key == "egress.bind_addr")
                egress.bind_addr = val;
            else if (key == "egress.port")
                egress.port = static_cast<uint16_t>(std::stoul(val));
            else if (key == "egress.max_buffer_bytes")
                egress.max_buffer_bytes = std::stoull(val);
            else if (key == "log_level")
                log_level = val;
        } catch (...) {
            // Silently ignore malformed CLI values
        }
    }
}

}  // namespace ltp
