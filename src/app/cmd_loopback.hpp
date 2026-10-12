#pragma once

/**
 * @file cmd_loopback.hpp
 * @brief `mpsk loopback`: transmitter, simulated channel and receiver, in software.
 *
 * Requirement 6.1. With no hardware it runs the whole chain, so the DSP can be
 * checked before a radio is involved. Three modes:
 *
 *   - a single run:  --frames N frames through the channel, one line per frame and a summary;
 *   - a sweep:       --sweep START:STOP:STEP of Eb/N0 in dB, one table row per point,
 *                    each point running until --target-errors bit errors or --max-bits bits;
 *   - with files:    --tx-out and --rx-out write the transmitted and the impaired samples
 *                    as cf32, for inspection or for `mpsk rx --in`.
 *
 * Exit status is 0 when the run completed, whatever the bit error rate was: the
 * report is the result, and a script reads the numbers.
 */

#include <iosfwd>
#include <string>
#include <vector>

namespace mpsk::app {

/**
 * @param args  The arguments after the subcommand name.
 * @return The process exit status.
 * @throws UsageError for a bad option or combination, std::invalid_argument for a
 *         configuration the libraries refuse, std::runtime_error for a file that
 *         cannot be written.
 */
int run_loopback(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace mpsk::app
