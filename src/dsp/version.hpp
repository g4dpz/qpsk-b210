#pragma once

/**
 * @file version.hpp
 * @brief Project version and layer identification.
 */

namespace mpsk {

/** @return The project version string, for example "0.1.0". */
const char* version_string();

namespace dsp {
/** @return A short name for this layer, used to check the library link graph. */
const char* layer_name();
}  // namespace dsp

}  // namespace mpsk
