#include "app/cli.hpp"

#include <algorithm>
#include <functional>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include "app/cmd_loopback.hpp"
#include "app/option_parser.hpp"
#include "dsp/version.hpp"
#ifdef MPSK_WITH_UHD
#include "radio/uhd_info.hpp"
#endif

namespace mpsk::app {

namespace {

using Handler = CommandHandler;

struct Command {
  std::string name;
  std::string summary;
  Handler run;
};

/**
 * A command that cannot run yet. With no UHD the reason is the build; with UHD it is
 * that the part is still to be written. Either way it says so, exits 1, and answers
 * --help.
 */
Handler unavailable(const std::string& name, const std::string& what, bool needs_radio, const std::string& planned) {
  return [name, what, needs_radio, planned](const std::vector<std::string>& args, std::ostream& out,
                                           std::ostream& err) {
    for (const std::string& a : args) {
      if (a == "--help" || a == "-h") {
        out << "Usage: mpsk " << name << " [options]\n\n" << what << "\n\n";
        out << (needs_radio ? "Not available yet: it needs the radio layer, " : "Not available yet: ") << planned
            << ".\n";
        return 0;
      }
    }
#ifndef MPSK_WITH_UHD
    if (needs_radio) {
      err << "mpsk " << name << ": built without UHD, so there is no way to reach a radio.\n"
          << "Reconfigure with -DMPSK_WITH_UHD=ON and rebuild. The offline commands (loopback) still work.\n";
      return 1;
    }
#endif
    err << "mpsk " << name << ": not available yet: " << planned << ".\n";
    return 1;
  };
}

int run_version(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  if (!args.empty() && args[0] != "--help" && args[0] != "-h") {
    throw UsageError("unexpected argument '" + args[0] + "'");
  }
  if (!args.empty()) {
    out << "Usage: mpsk version\n\nPrints the version and, when built with UHD, the UHD library versions.\n";
    return 0;
  }
  out << "mpsk " << mpsk::version_string() << "\n";
#ifdef MPSK_WITH_UHD
  out << "UHD radio layer: enabled\n"
      << "  UHD headers : " << mpsk::radio::uhd_compiled_version() << "\n"
      << "  UHD library : " << mpsk::radio::uhd_runtime_version() << "\n";
  if (!mpsk::radio::uhd_versions_match()) {
    err << "error: this build mixes UHD headers " << mpsk::radio::uhd_compiled_version() << " with UHD library "
        << mpsk::radio::uhd_runtime_version()
        << ".\nAn older UHD install is shadowing the intended one. "
           "Rebuild with -DMPSK_UHD_PREFIX=<prefix> after removing or "
           "hiding the stale headers.\n";
    return 1;
  }
#else
  (void)err;
  out << "UHD radio layer: not built (MPSK_WITH_UHD=OFF)\n";
#endif
  return 0;
}

const std::vector<Command>& commands() {
  static const std::vector<Command> table = {
      {"loopback", "Transmitter, simulated channel and receiver in software; BER and sweeps", run_loopback},
      {"echo-sim", "Echo test between two simulated nodes",
       unavailable("echo-sim", "Half-duplex echo test between two simulated nodes, with a channel in each direction.",
                   false, "the echo logic is task 11")},
      {"tx", "Transmit frames to a USRP, or write them to a file",
       unavailable("tx", "Transmit frames to a USRP, or write the samples to a file.", true,
                   "the radio layer is tasks 12 and 13")},
      {"rx", "Receive from a USRP, or from a file, and report each frame",
       unavailable("rx", "Receive from a USRP, or read samples from a file, and report each frame.", true,
                   "the radio layer is tasks 12 and 13")},
      {"echo-init", "Echo test, initiator role (two USRPs)",
       unavailable("echo-init", "Echo test, initiator role: send frames and measure the echoes.", true,
                   "the echo logic and the radio layer are tasks 11 to 13")},
      {"echo-resp", "Echo test, responder role (two USRPs)",
       unavailable("echo-resp", "Echo test, responder role: decode each frame and send it back.", true,
                   "the echo logic and the radio layer are tasks 11 to 13")},
      {"version", "Print the version and the UHD library versions", run_version},
  };
  return table;
}

const Command* find_command(const std::string& name) {
  for (const Command& c : commands()) {
    if (c.name == name) return &c;
  }
  return nullptr;
}

}  // namespace

std::string top_level_usage() {
  std::ostringstream o;
  o << "Usage: mpsk <command> [options]\n\n"
       "An M-PSK transceiver for the USRP B200-mini. QPSK is enabled; 8-PSK comes in stage 2.\n\n"
       "Commands:\n";
  std::size_t width = 0;
  for (const Command& c : commands()) width = std::max(width, c.name.size());
  for (const Command& c : commands()) {
    o << "  " << c.name << std::string(width - c.name.size() + 2, ' ') << c.summary << "\n";
  }
  o << "  help" << std::string(width - 4 + 2, ' ') << "Show this text, or 'mpsk help <command>'\n\n"
       "Run 'mpsk <command> --help' for a command's options.\n";
  return o.str();
}

int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  if (args.empty()) {
    err << "mpsk: no command given\n\n" << top_level_usage();
    return 2;
  }
  std::string name = args[0];
  std::vector<std::string> rest(args.begin() + 1, args.end());

  if (name == "--help" || name == "-h") {
    out << top_level_usage();
    return 0;
  }
  if (name == "help") {
    if (rest.empty()) {
      out << top_level_usage();
      return 0;
    }
    name = rest[0];
    rest = {"--help"};
  } else if (name == "--version") {
    name = "version";
    rest.clear();
  }

  const Command* cmd = find_command(name);
  if (!cmd) {
    std::vector<std::string> names;
    for (const Command& c : commands()) names.push_back(c.name);
    err << "mpsk: unknown command '" << name << "'";
    if (const auto near = closest_match(name, names)) err << " (did you mean '" << *near << "'?)";
    err << "\nTry 'mpsk --help'.\n";
    return 2;
  }

  return run_guarded(name, cmd->run, rest, out, err);
}

int run_guarded(const std::string& name, const CommandHandler& handler, const std::vector<std::string>& args,
                std::ostream& out, std::ostream& err) {
  try {
    return handler(args, out, err);
  } catch (const UsageError& e) {
    err << "mpsk " << name << ": error: " << e.what() << "\nTry 'mpsk " << name << " --help'.\n";
    return 2;
  } catch (const std::invalid_argument& e) {
    // A configuration the libraries refuse: the user's settings, not a bug.
    err << "mpsk " << name << ": error: " << e.what() << "\nTry 'mpsk " << name << " --help'.\n";
    return 2;
  } catch (const std::logic_error& e) {
    err << "mpsk " << name << ": internal error: " << e.what() << "\n";
    return 3;
  } catch (const std::exception& e) {
    err << "mpsk " << name << ": error: " << e.what() << "\n";
    return 1;
  } catch (...) {
    err << "mpsk " << name << ": internal error: unknown exception\n";
    return 3;
  }
}

}  // namespace mpsk::app
