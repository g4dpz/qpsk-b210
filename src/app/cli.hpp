#pragma once

/**
 * @file cli.hpp
 * @brief The `mpsk` command line: finds the subcommand and runs it.
 *
 * Exit status:
 *   0  success (including `--help`);
 *   1  the command ran and failed (a file could not be written, a radio is missing,
 *      a feature is not built), or is not available in this build;
 *   2  the command line was wrong: unknown command or option, a bad value, or a
 *      configuration the libraries refuse. The message names the option, and a
 *      pointer to `--help` follows;
 *   3  an internal error (a bug in `mpsk`, not in the command line).
 *
 * Everything is written to the streams passed in, never to std::cout directly, so
 * the tests run the real thing and read what it printed.
 */

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace mpsk::app {

/** @param args  The arguments after the program name. */
int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

using CommandHandler = std::function<int(const std::vector<std::string>&, std::ostream&, std::ostream&)>;

/**
 * Runs @p handler and turns whatever it throws into a message and an exit status:
 * UsageError and std::invalid_argument (a setting the libraries refuse) are the user's
 * mistakes, status 2 with a pointer to --help; other std::logic_error and anything that is
 * not a std::exception are bugs, status 3; any other std::exception is a failure of the
 * run, such as a file that cannot be written, status 1. Public so the mapping can be tested
 * with handlers that throw each kind.
 */
int run_guarded(const std::string& command_name, const CommandHandler& handler,
                const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

/** The overview printed by `mpsk --help`. */
std::string top_level_usage();

}  // namespace mpsk::app
