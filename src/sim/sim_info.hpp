#pragma once

/**
 * @file sim_info.hpp
 * @brief Layer identification for the simulation library.
 */

namespace mpsk::sim {
/** @return A short name for this layer, used to check the library link graph. */
const char* layer_name();
}  // namespace mpsk::sim
