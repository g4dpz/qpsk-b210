#pragma once

/**
 * @file option_parser.hpp
 * @brief A small command-line option parser: `--name value`, typed, with help text.
 *
 * Hand-written on purpose (design decision 3: no Boost.Program_options). It does
 * only what the `mpsk` subcommands need:
 *
 *   - options are `--name value` or `--name=value`; a flag is `--name` alone;
 *   - each option has a type (string, integer, number, flag), an optional default,
 *     and optionally a range or a list of allowed words;
 *   - a value is converted strictly: the whole text must be a number, a number
 *     must be finite, an integer must be whole, and a range or word list is
 *     enforced. A value may begin with a single `-` (`--cfo-hz -250`);
 *   - every failure is a UsageError whose message names the option and says what
 *     was wrong, and for an unknown option offers the closest known one;
 *   - `--help` or `-h` anywhere asks for the usage text and skips the rest of the
 *     checking, so a half-typed command line can still be asked for help.
 *
 * Programming mistakes (asking for an option that was never declared, declaring a
 * name twice) throw std::logic_error instead, because they are bugs in `mpsk`, not
 * mistakes by its user.
 */

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mpsk::app {

/** The user gave something unusable. The message names the option and the problem. */
class UsageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class OptionKind { String, Integer, Number, Flag };

/** One declared option. Filled in by OptionParser's add_* functions. */
struct OptionSpec {
  std::string name;             ///< without the leading dashes: "cfo-hz"
  OptionKind kind = OptionKind::String;
  std::string metavar;          ///< placeholder in the usage text: "HZ"
  std::string help;
  std::string group;            ///< heading in the usage text

  std::optional<std::string> default_string;
  std::optional<long long> default_integer;
  std::optional<double> default_number;

  std::vector<std::string> choices;   ///< String only: the allowed words, matched without regard to case
  std::optional<double> minimum;      ///< Integer and Number: inclusive
  std::optional<double> maximum;
};

/** The result of parsing: what was given, with the defaults filled in. */
class ParsedOptions {
 public:
  /** One option's state. Public only so the parser's helpers can fill it in; read through the accessors. */
  struct Entry {
    OptionKind kind = OptionKind::String;
    bool given = false;
    bool has_value = false;
    std::string text;
    long long integer = 0;
    double number = 0.0;
    bool flag = false;
  };

  /** True if the option appeared on the command line (not just defaulted). */
  bool given(const std::string& name) const;

  /** True if the option has a value to read: it was given, or it has a default. */
  bool has_value(const std::string& name) const;

  /** @throws std::logic_error if the option is undeclared, not a string, or has no value. */
  const std::string& str(const std::string& name) const;
  /** @throws std::logic_error as for str(). */
  long long integer(const std::string& name) const;
  /** An Integer option can also be read as a number. @throws std::logic_error as for str(). */
  double number(const std::string& name) const;
  /** False unless the flag was given. @throws std::logic_error if undeclared or not a flag. */
  bool flag(const std::string& name) const;

 private:
  friend class OptionParser;
  const Entry& entry(const std::string& name) const;
  std::map<std::string, Entry> entries_;
};

struct ParseOutcome {
  bool help_requested = false;   ///< `--help` or `-h` was present; values are not filled in
  ParsedOptions values;
};

class OptionParser {
 public:
  /**
   * @param command  What the usage line calls the command: "mpsk loopback".
   * @param summary  One or two sentences for the top of the usage text.
   */
  OptionParser(std::string command, std::string summary);

  /** Options declared after this are listed under @p title in the usage text. */
  OptionParser& group(const std::string& title);

  /** @param choices  If not empty, the only values allowed; matched ignoring case and stored in the form given here. */
  OptionParser& add_string(const std::string& name, const std::string& metavar, const std::string& help,
                           std::optional<std::string> default_value = std::nullopt,
                           std::vector<std::string> choices = {});

  OptionParser& add_integer(const std::string& name, const std::string& metavar, const std::string& help,
                            std::optional<long long> default_value = std::nullopt,
                            std::optional<long long> minimum = std::nullopt,
                            std::optional<long long> maximum = std::nullopt);

  OptionParser& add_number(const std::string& name, const std::string& metavar, const std::string& help,
                           std::optional<double> default_value = std::nullopt,
                           std::optional<double> minimum = std::nullopt,
                           std::optional<double> maximum = std::nullopt);

  OptionParser& add_flag(const std::string& name, const std::string& help);

  /**
   * Parses the arguments that follow the subcommand name.
   * @throws UsageError for an unknown option, a missing or malformed or out-of-range
   *         value, an option given twice, or a stray argument.
   */
  ParseOutcome parse(const std::vector<std::string>& args) const;

  /** The usage text, ending in a newline. */
  std::string usage() const;

  const std::string& command() const { return command_; }

 private:
  const OptionSpec* find(const std::string& name) const;
  void declare(OptionSpec spec);

  std::string command_;
  std::string summary_;
  std::string current_group_ = "Options";
  std::vector<OptionSpec> specs_;
};

/**
 * Splits "A:B:C" into three numbers, for a range such as an Eb/N0 sweep.
 * @param what  What the text was for, used in the message: "--sweep".
 * @throws UsageError unless the text is exactly three numbers separated by colons.
 */
std::vector<double> parse_colon_triple(const std::string& what, const std::string& text);

/**
 * The candidate closest to @p word by edit distance, if it is within @p max_distance.
 * Used to suggest a fix for a misspelled option or command.
 */
std::optional<std::string> closest_match(const std::string& word, const std::vector<std::string>& candidates,
                                         std::size_t max_distance = 2);

}  // namespace mpsk::app
