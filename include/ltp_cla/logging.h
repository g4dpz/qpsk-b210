#pragma once

#include <spdlog/spdlog.h>
#include <string>

namespace ltp {

/// Configure the global spdlog log level from a string.
/// Valid levels: trace, debug, info, warn, error, critical, off.
inline void set_log_level(const std::string& level) {
    if (level == "trace")         spdlog::set_level(spdlog::level::trace);
    else if (level == "debug")    spdlog::set_level(spdlog::level::debug);
    else if (level == "info")     spdlog::set_level(spdlog::level::info);
    else if (level == "warn")     spdlog::set_level(spdlog::level::warn);
    else if (level == "error")    spdlog::set_level(spdlog::level::err);
    else if (level == "critical") spdlog::set_level(spdlog::level::critical);
    else if (level == "off")      spdlog::set_level(spdlog::level::off);
    else                          spdlog::set_level(spdlog::level::info);
}

}  // namespace ltp
