#include "app/option_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mpsk::app {

namespace {

std::string fmt(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.10g", v);
  return buf;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/**
 * Only digits, sign, point and exponent: no spaces, no hex, no "nan" or "inf" spelled out.
 * strtod would accept all of those, and a value like "0x10" or " 5" is a typing mistake
 * more often than an intention.
 */
bool plain_numeric_text(const std::string& s) {
  if (s.empty()) return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return std::isdigit(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
  });
}

enum class NumberStatus { Ok, NotANumber, NotFinite };

NumberStatus parse_number_text(const std::string& s, double& out) {
  if (!plain_numeric_text(s)) return NumberStatus::NotANumber;
  errno = 0;
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + s.size()) return NumberStatus::NotANumber;
  if (!std::isfinite(v) || errno == ERANGE) return NumberStatus::NotFinite;
  out = v;
  return NumberStatus::Ok;
}

/** A whole number, written as 4, -3, or in a form like 1e6 that is exactly whole. */
enum class IntegerStatus { Ok, NotANumber, NotWhole, TooBig };

IntegerStatus parse_integer_text(const std::string& s, long long& out) {
  if (!plain_numeric_text(s)) return IntegerStatus::NotANumber;
  // Plain digits first: exact for the whole range of long long.
  {
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() + s.size()) {
      if (errno == ERANGE) return IntegerStatus::TooBig;
      out = v;
      return IntegerStatus::Ok;
    }
  }
  double d = 0.0;
  const NumberStatus n = parse_number_text(s, d);
  if (n == NumberStatus::NotANumber) return IntegerStatus::NotANumber;
  if (n == NumberStatus::NotFinite) return IntegerStatus::TooBig;
  if (d != std::floor(d)) return IntegerStatus::NotWhole;
  if (std::fabs(d) > 9e15) return IntegerStatus::TooBig;   // beyond what a double holds exactly
  out = static_cast<long long>(d);
  return IntegerStatus::Ok;
}

std::string quoted_option(const std::string& name) { return "option '--" + name + "'"; }

}  // namespace

// ---- ParsedOptions ----------------------------------------------------------

const ParsedOptions::Entry& ParsedOptions::entry(const std::string& name) const {
  const auto it = entries_.find(name);
  if (it == entries_.end()) throw std::logic_error("option '--" + name + "' was never declared");
  return it->second;
}

bool ParsedOptions::given(const std::string& name) const { return entry(name).given; }
bool ParsedOptions::has_value(const std::string& name) const { return entry(name).has_value; }

const std::string& ParsedOptions::str(const std::string& name) const {
  const Entry& e = entry(name);
  if (e.kind != OptionKind::String) throw std::logic_error("option '--" + name + "' is not a string option");
  if (!e.has_value) throw std::logic_error("option '--" + name + "' has no value and no default");
  return e.text;
}

long long ParsedOptions::integer(const std::string& name) const {
  const Entry& e = entry(name);
  if (e.kind != OptionKind::Integer) throw std::logic_error("option '--" + name + "' is not an integer option");
  if (!e.has_value) throw std::logic_error("option '--" + name + "' has no value and no default");
  return e.integer;
}

double ParsedOptions::number(const std::string& name) const {
  const Entry& e = entry(name);
  if (e.kind != OptionKind::Number && e.kind != OptionKind::Integer) {
    throw std::logic_error("option '--" + name + "' is not a numeric option");
  }
  if (!e.has_value) throw std::logic_error("option '--" + name + "' has no value and no default");
  return e.kind == OptionKind::Integer ? static_cast<double>(e.integer) : e.number;
}

bool ParsedOptions::flag(const std::string& name) const {
  const Entry& e = entry(name);
  if (e.kind != OptionKind::Flag) throw std::logic_error("option '--" + name + "' is not a flag");
  return e.flag;
}

// ---- OptionParser: declaring ------------------------------------------------

OptionParser::OptionParser(std::string command, std::string summary)
    : command_(std::move(command)), summary_(std::move(summary)) {}

OptionParser& OptionParser::group(const std::string& title) {
  current_group_ = title;
  return *this;
}

void OptionParser::declare(OptionSpec spec) {
  if (spec.name.empty() || spec.name[0] == '-' || spec.name.find('=') != std::string::npos ||
      spec.name.find(' ') != std::string::npos) {
    throw std::logic_error("bad option name '" + spec.name + "'");
  }
  if (spec.name == "help") throw std::logic_error("'--help' is built in");
  if (find(spec.name)) throw std::logic_error("option '--" + spec.name + "' is declared twice");
  spec.group = current_group_;
  specs_.push_back(std::move(spec));
}

OptionParser& OptionParser::add_string(const std::string& name, const std::string& metavar,
                                       const std::string& help, std::optional<std::string> default_value,
                                       std::vector<std::string> choices) {
  OptionSpec s;
  s.name = name;
  s.kind = OptionKind::String;
  s.metavar = metavar;
  s.help = help;
  s.choices = std::move(choices);
  if (default_value && !s.choices.empty()) {
    const bool listed = std::any_of(s.choices.begin(), s.choices.end(),
                                    [&](const std::string& c) { return lower(c) == lower(*default_value); });
    if (!listed) throw std::logic_error("the default of '--" + name + "' is not among its choices");
  }
  s.default_string = std::move(default_value);
  declare(std::move(s));
  return *this;
}

OptionParser& OptionParser::add_integer(const std::string& name, const std::string& metavar,
                                        const std::string& help, std::optional<long long> default_value,
                                        std::optional<long long> minimum, std::optional<long long> maximum) {
  OptionSpec s;
  s.name = name;
  s.kind = OptionKind::Integer;
  s.metavar = metavar;
  s.help = help;
  s.default_integer = default_value;
  if (minimum) s.minimum = static_cast<double>(*minimum);
  if (maximum) s.maximum = static_cast<double>(*maximum);
  declare(std::move(s));
  return *this;
}

OptionParser& OptionParser::add_number(const std::string& name, const std::string& metavar,
                                       const std::string& help, std::optional<double> default_value,
                                       std::optional<double> minimum, std::optional<double> maximum) {
  OptionSpec s;
  s.name = name;
  s.kind = OptionKind::Number;
  s.metavar = metavar;
  s.help = help;
  s.default_number = default_value;
  s.minimum = minimum;
  s.maximum = maximum;
  declare(std::move(s));
  return *this;
}

OptionParser& OptionParser::add_flag(const std::string& name, const std::string& help) {
  OptionSpec s;
  s.name = name;
  s.kind = OptionKind::Flag;
  s.help = help;
  declare(std::move(s));
  return *this;
}

const OptionSpec* OptionParser::find(const std::string& name) const {
  for (const OptionSpec& s : specs_) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

// ---- OptionParser: parsing --------------------------------------------------

namespace {

void store_value(const OptionSpec& spec, const std::string& text, ParsedOptions::Entry& e) {
  const std::string who = quoted_option(spec.name);
  switch (spec.kind) {
    case OptionKind::String: {
      if (spec.choices.empty()) {
        e.text = text;
      } else {
        const std::string want = lower(text);
        const auto it = std::find_if(spec.choices.begin(), spec.choices.end(),
                                     [&](const std::string& c) { return lower(c) == want; });
        if (it == spec.choices.end()) {
          std::string list;
          for (const std::string& c : spec.choices) list += (list.empty() ? "" : ", ") + c;
          throw UsageError(who + ": '" + text + "' is not one of: " + list);
        }
        e.text = *it;
      }
      break;
    }
    case OptionKind::Integer: {
      long long v = 0;
      switch (parse_integer_text(text, v)) {
        case IntegerStatus::Ok: break;
        case IntegerStatus::NotANumber: throw UsageError(who + ": '" + text + "' is not a number");
        case IntegerStatus::NotWhole: throw UsageError(who + ": '" + text + "' is not a whole number");
        case IntegerStatus::TooBig: throw UsageError(who + ": '" + text + "' is too large");
      }
      const double d = static_cast<double>(v);
      if (spec.minimum && d < *spec.minimum) {
        throw UsageError(who + ": " + text + " is below the minimum of " + fmt(*spec.minimum));
      }
      if (spec.maximum && d > *spec.maximum) {
        throw UsageError(who + ": " + text + " is above the maximum of " + fmt(*spec.maximum));
      }
      e.integer = v;
      break;
    }
    case OptionKind::Number: {
      double v = 0.0;
      switch (parse_number_text(text, v)) {
        case NumberStatus::Ok: break;
        case NumberStatus::NotANumber: throw UsageError(who + ": '" + text + "' is not a number");
        case NumberStatus::NotFinite: throw UsageError(who + ": '" + text + "' is not a finite number");
      }
      if (spec.minimum && v < *spec.minimum) {
        throw UsageError(who + ": " + text + " is below the minimum of " + fmt(*spec.minimum));
      }
      if (spec.maximum && v > *spec.maximum) {
        throw UsageError(who + ": " + text + " is above the maximum of " + fmt(*spec.maximum));
      }
      e.number = v;
      break;
    }
    case OptionKind::Flag: break;
  }
  e.has_value = true;
}

}  // namespace

ParseOutcome OptionParser::parse(const std::vector<std::string>& args) const {
  ParseOutcome out;
  for (const std::string& a : args) {
    if (a == "--help" || a == "-h") {
      out.help_requested = true;
      return out;
    }
  }

  // Start every option at its default.
  for (const OptionSpec& s : specs_) {
    ParsedOptions::Entry e;
    e.kind = s.kind;
    switch (s.kind) {
      case OptionKind::String:
        if (s.default_string) { e.text = *s.default_string; e.has_value = true; }
        break;
      case OptionKind::Integer:
        if (s.default_integer) { e.integer = *s.default_integer; e.has_value = true; }
        break;
      case OptionKind::Number:
        if (s.default_number) { e.number = *s.default_number; e.has_value = true; }
        break;
      case OptionKind::Flag: break;
    }
    out.values.entries_[s.name] = e;
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg.size() < 3 || arg.compare(0, 2, "--") != 0) {
      throw UsageError("unexpected argument '" + arg + "' (options start with --, and take their value as the next argument)");
    }

    std::string name = arg.substr(2);
    std::optional<std::string> inline_value;
    const std::size_t eq = name.find('=');
    if (eq != std::string::npos) {
      inline_value = name.substr(eq + 1);
      name.erase(eq);
    }

    const OptionSpec* spec = find(name);
    if (!spec) {
      std::vector<std::string> names;
      for (const OptionSpec& s : specs_) names.push_back(s.name);
      std::string msg = "unknown option '--" + name + "'";
      if (const auto near = closest_match(name, names)) msg += " (did you mean '--" + *near + "'?)";
      throw UsageError(msg);
    }

    ParsedOptions::Entry& e = out.values.entries_[name];
    if (e.given) throw UsageError(quoted_option(name) + " was given twice");
    e.given = true;

    if (spec->kind == OptionKind::Flag) {
      if (inline_value) throw UsageError("flag '--" + name + "' takes no value");
      e.flag = true;
      continue;
    }

    std::string value;
    if (inline_value) {
      value = *inline_value;
    } else {
      if (i + 1 >= args.size() || args[i + 1].compare(0, 2, "--") == 0) {
        throw UsageError(quoted_option(name) + " needs a value" +
                         (spec->metavar.empty() ? "" : " (" + spec->metavar + ")"));
      }
      value = args[++i];
    }
    if (value.empty()) throw UsageError(quoted_option(name) + " needs a value (it was empty)");
    store_value(*spec, value, e);
  }
  return out;
}

// ---- OptionParser: usage text ----------------------------------------------

std::string OptionParser::usage() const {
  std::ostringstream o;
  o << "Usage: " << command_ << " [options]\n\n" << summary_ << "\n";

  // Left column for each option, so the help text lines up.
  auto left_of = [](const OptionSpec& s) {
    std::string l = "--" + s.name;
    if (s.kind == OptionKind::Flag) return l;
    if (!s.choices.empty()) {
      std::string list;
      for (const std::string& c : s.choices) list += (list.empty() ? "" : "|") + c;
      return l + " " + list;
    }
    return l + " " + (s.metavar.empty() ? "VALUE" : s.metavar);
  };
  std::size_t width = std::string("-h, --help").size();
  for (const OptionSpec& s : specs_) width = std::max(width, left_of(s).size());
  width = std::min<std::size_t>(width, 30);

  auto line = [&](const std::string& left, const std::string& text) {
    o << "  " << left;
    if (left.size() >= width) {
      o << "\n  " << std::string(width, ' ');
    } else {
      o << std::string(width - left.size(), ' ');
    }
    o << "  " << text << "\n";
  };

  std::vector<std::string> groups;
  for (const OptionSpec& s : specs_) {
    if (std::find(groups.begin(), groups.end(), s.group) == groups.end()) groups.push_back(s.group);
  }
  for (const std::string& g : groups) {
    o << "\n" << g << ":\n";
    for (const OptionSpec& s : specs_) {
      if (s.group != g) continue;
      std::string text = s.help;
      std::string notes;
      auto note = [&](const std::string& n) { notes += (notes.empty() ? "" : "; ") + n; };
      if (s.minimum && s.maximum) note("range " + fmt(*s.minimum) + " to " + fmt(*s.maximum));
      else if (s.minimum) note("at least " + fmt(*s.minimum));
      else if (s.maximum) note("at most " + fmt(*s.maximum));
      if (s.default_string) note("default " + *s.default_string);
      if (s.default_integer) note("default " + std::to_string(*s.default_integer));
      if (s.default_number) note("default " + fmt(*s.default_number));
      if (!notes.empty()) text += " (" + notes + ")";
      line(left_of(s), text);
    }
  }
  o << "\n";
  line("-h, --help", "Show this text");
  return o.str();
}

// ---- Helpers ----------------------------------------------------------------

std::vector<double> parse_colon_triple(const std::string& what, const std::string& text) {
  std::vector<std::string> parts;
  std::string cur;
  for (const char c : text) {
    if (c == ':') {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  parts.push_back(cur);

  std::vector<double> out;
  bool ok = parts.size() == 3;
  for (const std::string& p : parts) {
    double v = 0.0;
    if (!ok || parse_number_text(p, v) != NumberStatus::Ok) {
      ok = false;
      break;
    }
    out.push_back(v);
  }
  if (!ok) {
    throw UsageError("option '" + what + "': '" + text +
                     "' should be three numbers separated by colons, START:STOP:STEP, for example 0:12:2");
  }
  return out;
}

std::optional<std::string> closest_match(const std::string& word, const std::vector<std::string>& candidates,
                                         std::size_t max_distance) {
  std::optional<std::string> best;
  std::size_t best_distance = max_distance + 1;
  for (const std::string& c : candidates) {
    // Classic edit distance, one row at a time.
    std::vector<std::size_t> prev(c.size() + 1), cur(c.size() + 1);
    for (std::size_t j = 0; j <= c.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= word.size(); ++i) {
      cur[0] = i;
      for (std::size_t j = 1; j <= c.size(); ++j) {
        const std::size_t substitute = prev[j - 1] + (word[i - 1] == c[j - 1] ? 0 : 1);
        cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, substitute});
      }
      std::swap(prev, cur);
    }
    const std::size_t d = prev[c.size()];
    if (d < best_distance) {
      best_distance = d;
      best = c;
    }
  }
  return best;
}

}  // namespace mpsk::app
