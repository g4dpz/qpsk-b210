#include "radio/uhd_info.hpp"

#include <uhd/version.hpp>

namespace mpsk::radio {

std::string uhd_compiled_version() { return UHD_VERSION_ABI_STRING; }

std::string uhd_runtime_version() { return uhd::get_version_string(); }

bool uhd_versions_match() {
  const std::string compiled = uhd_compiled_version();  // e.g. "4.10.0"
  const std::string runtime = uhd_runtime_version();    // e.g. "4.10.0.0-0-unknown"

  if (runtime.compare(0, compiled.size(), compiled) != 0) return false;

  // Require a boundary after the compiled string, so that "4.1.0" does not
  // match a runtime of "4.10.0.0". The next character must end the version
  // number: a '.', a '-', or the end of the string.
  if (runtime.size() == compiled.size()) return true;
  const char next = runtime[compiled.size()];
  return next == '.' || next == '-';
}

}  // namespace mpsk::radio
