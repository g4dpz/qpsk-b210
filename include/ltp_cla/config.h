#pragma once

#include "ltp_cla/ltp_engine.h"
#include "ltp_cla/convergence_layer_adapter.h"
#include "ltp_cla/tcp_ingress.h"
#include "ltp_cla/tcp_egress.h"

#include <string>
#include <vector>

namespace ltp {

// ---------------------------------------------------------------------------
// Aggregate configuration
// ---------------------------------------------------------------------------

struct LtpClaConfig {
    LtpEngineConfig ltp;
    ClaConfig cla;
    IngressConfig ingress;
    EgressConfig egress;
    std::string log_level = "info";

    /// Validate all parameter ranges. Returns a list of error messages.
    /// Empty list means valid.
    std::vector<std::string> validate() const;

    /// Serialize to JSON string.
    std::string to_json() const;

    /// Write JSON to a file.
    void to_json_file(const std::string& path) const;

    /// Deserialize from JSON string.
    static LtpClaConfig from_json(const std::string& json_str);

    /// Load from a JSON file.
    static LtpClaConfig from_json_file(const std::string& path);

    /// Return default configuration.
    static LtpClaConfig defaults();

    /// Apply command-line argument overrides.
    /// Supported args: --ltp.local_engine_id=N, --ltp.max_segment_size=N, etc.
    void apply_cli_overrides(int argc, const char* const* argv);

    bool operator==(const LtpClaConfig& other) const;
};

}  // namespace ltp
