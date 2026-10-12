#pragma once

#include <string>

/**
 * @file uhd_info.hpp
 * @brief Reports which UHD the program was compiled and linked against.
 *
 * This header must stay free of UHD includes. It is the only thing the rest of
 * the program sees of the radio layer's UHD usage.
 */

namespace mpsk::radio {

/**
 * @return The UHD version of the headers this library was compiled with, for
 *         example "4.10.0".
 */
std::string uhd_compiled_version();

/**
 * @return The version string of the UHD library loaded at run time, for
 *         example "4.10.0.0-0-unknown".
 */
std::string uhd_runtime_version();

/**
 * Checks that the headers and the library come from the same UHD release.
 *
 * A machine can hold several UHD installs. Compiling against one install's
 * headers and linking another's library builds cleanly but can fail in
 * unpredictable ways, so callers should treat a mismatch as an error.
 *
 * @return true if the runtime version equals the compiled version, or extends
 *         it with a further ".<digits>" component (4.10.0 matches 4.10.0.0).
 */
bool uhd_versions_match();

}  // namespace mpsk::radio
