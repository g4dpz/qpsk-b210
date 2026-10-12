#pragma once

/**
 * @file link_info.hpp
 * @brief Layer identification for the link library.
 */

namespace mpsk::link {
/** @return A short name for this layer, used to check the library link graph. */
const char* layer_name();
}  // namespace mpsk::link
