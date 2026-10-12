// Tests for the command-line option parser (task 9, requirement 10 on structure; the
// design's "hand-written parser, errors that name the bad option").

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/option_parser.hpp"
#include "test_harness.hpp"

using namespace mpsk::app;
using Args = std::vector<std::string>;

namespace {

OptionParser sample() {
  OptionParser p("mpsk demo", "A demo command.");
  p.group("Link");
  p.add_string("mod", "", "Modulation", std::string("qpsk"), {"qpsk", "8psk"});
  p.add_integer("sps", "N", "Samples per symbol", 4, 2, 64);
  p.add_number("rolloff", "BETA", "Roll-off", 0.35, 0.1, 1.0);
  p.group("Channel");
  p.add_number("cfo-hz", "HZ", "Carrier offset", 0.0);
  p.add_number("ebn0", "DB", "Noise level; absent means none");
  p.add_string("out", "FILE", "Output file");
  p.add_integer("count", "N", "How many");
  p.add_flag("quiet", "Say less");
  return p;
}

/** The message of the UsageError that parsing @p args raises, or "" if it raises none. */
std::string error_of(const Args& args) {
  try {
    sample().parse(args);
  } catch (const UsageError& e) {
    return e.what();
  }
  return "";
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

// ---- Defaults and overrides -------------------------------------------------

TEST(defaults_apply_when_nothing_is_given) {
  const ParseOutcome o = sample().parse({});
  CHECK(!o.help_requested);
  CHECK(o.values.str("mod") == "qpsk");
  CHECK(o.values.integer("sps") == 4);
  CHECK_NEAR(o.values.number("rolloff"), 0.35, 1e-15);
  CHECK_NEAR(o.values.number("cfo-hz"), 0.0, 1e-15);
  CHECK(!o.values.flag("quiet"));
  CHECK(!o.values.given("sps"));
  CHECK(o.values.has_value("sps"));
}

TEST(an_option_with_no_default_has_no_value_until_given) {
  const ParseOutcome none = sample().parse({});
  CHECK(!none.values.has_value("ebn0"));
  CHECK(!none.values.given("ebn0"));
  CHECK_THROWS(none.values.number("ebn0"));      // asking for what is not there is a bug
  CHECK_THROWS(none.values.str("out"));
  CHECK_THROWS(none.values.integer("count"));

  const ParseOutcome some = sample().parse({"--ebn0", "7.5", "--out", "x.cf32", "--count", "3"});
  CHECK(some.values.has_value("ebn0"));
  CHECK_NEAR(some.values.number("ebn0"), 7.5, 1e-15);
  CHECK(some.values.str("out") == "x.cf32");
  CHECK(some.values.integer("count") == 3);
}

TEST(given_values_override_the_defaults_and_say_they_were_given) {
  const ParseOutcome o = sample().parse({"--sps", "8", "--rolloff", "0.5", "--mod", "8psk", "--quiet"});
  CHECK(o.values.integer("sps") == 8);
  CHECK_NEAR(o.values.number("rolloff"), 0.5, 1e-15);
  CHECK(o.values.str("mod") == "8psk");
  CHECK(o.values.flag("quiet"));
  CHECK(o.values.given("sps"));
  CHECK(o.values.given("quiet"));
  CHECK(!o.values.given("cfo-hz"));
}

TEST(the_order_of_options_does_not_matter) {
  const ParseOutcome a = sample().parse({"--sps", "8", "--quiet", "--cfo-hz", "5"});
  const ParseOutcome b = sample().parse({"--cfo-hz", "5", "--quiet", "--sps", "8"});
  CHECK(a.values.integer("sps") == b.values.integer("sps"));
  CHECK_NEAR(a.values.number("cfo-hz"), b.values.number("cfo-hz"), 0);
}

TEST(an_equals_sign_works_like_a_space) {
  const ParseOutcome o = sample().parse({"--sps=8", "--out=a=b.cf32"});
  CHECK(o.values.integer("sps") == 8);
  CHECK(o.values.str("out") == "a=b.cf32");   // only the first '=' splits
}

TEST(a_value_may_start_with_a_single_minus) {
  const ParseOutcome o = sample().parse({"--cfo-hz", "-250", "--ebn0", "-3.5"});
  CHECK_NEAR(o.values.number("cfo-hz"), -250.0, 1e-12);
  CHECK_NEAR(o.values.number("ebn0"), -3.5, 1e-12);
  CHECK_NEAR(sample().parse({"--cfo-hz=-1e3"}).values.number("cfo-hz"), -1000.0, 1e-12);
}

// ---- Numbers and words ------------------------------------------------------

TEST(numbers_are_read_in_the_usual_forms) {
  CHECK_NEAR(sample().parse({"--cfo-hz", "1e3"}).values.number("cfo-hz"), 1000.0, 1e-12);
  CHECK_NEAR(sample().parse({"--cfo-hz", "+2.5"}).values.number("cfo-hz"), 2.5, 1e-12);
  CHECK_NEAR(sample().parse({"--cfo-hz", ".5"}).values.number("cfo-hz"), 0.5, 1e-12);
  CHECK_NEAR(sample().parse({"--cfo-hz", "5."}).values.number("cfo-hz"), 5.0, 1e-12);
}

TEST(an_integer_may_be_written_as_an_exactly_whole_number_in_other_forms) {
  CHECK(sample().parse({"--count", "1e6"}).values.integer("count") == 1000000);
  CHECK(sample().parse({"--count", "4.0"}).values.integer("count") == 4);
  CHECK(sample().parse({"--count", "+7"}).values.integer("count") == 7);
  CHECK(sample().parse({"--count", "-7"}).values.integer("count") == -7);
  CHECK(sample().parse({"--count", "9000000000"}).values.integer("count") == 9000000000LL);
}

TEST(an_integer_option_can_be_read_as_a_number) {
  CHECK_NEAR(sample().parse({"--sps", "8"}).values.number("sps"), 8.0, 0);
}

TEST(words_are_matched_without_regard_to_case_and_stored_as_declared) {
  CHECK(sample().parse({"--mod", "QPSK"}).values.str("mod") == "qpsk");
  CHECK(sample().parse({"--mod", "8Psk"}).values.str("mod") == "8psk");
}

// ---- Errors name the option -------------------------------------------------

TEST(an_unknown_option_is_named_and_the_closest_known_one_is_suggested) {
  const std::string e = error_of({"--rolof", "0.3"});
  CHECK(contains(e, "unknown option '--rolof'"));
  CHECK(contains(e, "did you mean '--rolloff'"));
  const std::string far = error_of({"--zzzzzzzz", "1"});
  CHECK(contains(far, "unknown option '--zzzzzzzz'"));
  CHECK(!contains(far, "did you mean"));
  CHECK(contains(error_of({"--cfo", "5"}), "'--cfo'"));
}

TEST(a_missing_value_is_reported_for_the_option) {
  CHECK(contains(error_of({"--sps"}), "option '--sps' needs a value"));
  CHECK(contains(error_of({"--sps"}), "(N)"));                       // and says what to give
  CHECK(contains(error_of({"--sps", "--quiet"}), "option '--sps' needs a value"));   // not swallowing the next option
  CHECK(contains(error_of({"--quiet", "--cfo-hz"}), "option '--cfo-hz' needs a value"));
  CHECK(contains(error_of({"--sps="}), "option '--sps' needs a value"));
  CHECK(contains(error_of({"--out", ""}), "option '--out' needs a value"));
}

TEST(a_malformed_number_names_the_option_and_the_text) {
  const std::string e = error_of({"--cfo-hz", "fast"});
  CHECK(contains(e, "option '--cfo-hz'"));
  CHECK(contains(e, "'fast' is not a number"));
  CHECK(contains(error_of({"--sps", "4.5"}), "'4.5' is not a whole number"));
  CHECK(contains(error_of({"--sps", "abc"}), "'abc' is not a number"));
  for (const char* bad : {"12abc", "1,5", "0x10", " 5", "5 ", "--", "e", "1e", "+", "-", "1..2"}) {
    CHECK(!error_of({"--cfo-hz", bad}).empty());
  }
}

TEST(values_that_are_not_finite_are_refused) {
  for (const char* bad : {"nan", "NaN", "inf", "-inf", "infinity", "1e999", "-1e999"}) {
    const std::string e = error_of({"--cfo-hz", bad});
    CHECK(!e.empty());
    CHECK(contains(e, "'--cfo-hz'"));
  }
  CHECK(contains(error_of({"--count", "1e999"}), "too large"));
  CHECK(contains(error_of({"--count", "99999999999999999999"}), "too large"));
  CHECK(contains(error_of({"--count", "1e17"}), "too large"));
}

TEST(a_value_outside_its_range_is_refused_with_the_limit) {
  CHECK(contains(error_of({"--sps", "1"}), "option '--sps': 1 is below the minimum of 2"));
  CHECK(contains(error_of({"--sps", "65"}), "option '--sps': 65 is above the maximum of 64"));
  CHECK(contains(error_of({"--rolloff", "0.05"}), "below the minimum of 0.1"));
  CHECK(contains(error_of({"--rolloff", "1.5"}), "above the maximum of 1"));
  // The limits themselves are allowed.
  CHECK(error_of({"--sps", "2"}).empty());
  CHECK(error_of({"--sps", "64"}).empty());
  CHECK(error_of({"--rolloff", "0.1"}).empty());
  CHECK(error_of({"--rolloff", "1"}).empty());
}

TEST(a_word_outside_its_choices_lists_them) {
  const std::string e = error_of({"--mod", "bpsk"});
  CHECK(contains(e, "option '--mod'"));
  CHECK(contains(e, "'bpsk' is not one of: qpsk, 8psk"));
}

TEST(an_option_given_twice_is_refused) {
  CHECK(contains(error_of({"--sps", "4", "--sps", "8"}), "option '--sps' was given twice"));
  CHECK(contains(error_of({"--quiet", "--quiet"}), "option '--quiet' was given twice"));
  CHECK(contains(error_of({"--sps=4", "--sps", "8"}), "was given twice"));
}

TEST(a_flag_takes_no_value) {
  CHECK(contains(error_of({"--quiet=yes"}), "flag '--quiet' takes no value"));
  // A word after a flag is a stray argument, not its value.
  CHECK(contains(error_of({"--quiet", "yes"}), "unexpected argument 'yes'"));
}

TEST(stray_arguments_are_refused) {
  CHECK(contains(error_of({"extra"}), "unexpected argument 'extra'"));
  CHECK(contains(error_of({"--sps", "4", "extra"}), "unexpected argument 'extra'"));
  CHECK(contains(error_of({"-x"}), "unexpected argument '-x'"));
  CHECK(contains(error_of({"--"}), "unexpected argument '--'"));
  CHECK(contains(error_of({"-250"}), "unexpected argument '-250'"));
}

// ---- Help -------------------------------------------------------------------

TEST(help_is_recognised_anywhere_and_skips_the_rest_of_the_checking) {
  CHECK(sample().parse({"--help"}).help_requested);
  CHECK(sample().parse({"-h"}).help_requested);
  CHECK(sample().parse({"--sps", "4", "--help"}).help_requested);
  // Even with a mistake elsewhere on the line.
  CHECK(sample().parse({"--nonsense", "--help"}).help_requested);
  CHECK(sample().parse({"--sps", "abc", "-h"}).help_requested);
}

TEST(the_usage_text_lists_every_option_with_its_default_and_range) {
  const std::string u = sample().usage();
  CHECK(contains(u, "Usage: mpsk demo [options]"));
  CHECK(contains(u, "A demo command."));
  CHECK(contains(u, "Link:"));
  CHECK(contains(u, "Channel:"));
  CHECK(contains(u, "--mod qpsk|8psk"));
  CHECK(contains(u, "Modulation (default qpsk)"));
  CHECK(contains(u, "--sps N"));
  CHECK(contains(u, "range 2 to 64"));
  CHECK(contains(u, "default 4"));
  CHECK(contains(u, "default 0.35"));
  CHECK(contains(u, "--cfo-hz HZ"));
  CHECK(contains(u, "--quiet"));
  CHECK(contains(u, "-h, --help"));
  CHECK(contains(u, "--ebn0 DB"));
  CHECK(!contains(u, "--ebn0 DB  Noise level; absent means none (default"));   // no default to show
  // Groups appear in the order they were declared.
  CHECK(u.find("Link:") < u.find("Channel:"));
  CHECK(u.back() == '\n');
}

// ---- Mistakes in the program itself ----------------------------------------

TEST(declaring_an_option_badly_is_a_logic_error_not_a_usage_error) {
  OptionParser p("x", "y");
  p.add_flag("quiet", "q");
  CHECK_THROWS(p.add_flag("quiet", "again"));
  CHECK_THROWS(p.add_flag("", "empty"));
  CHECK_THROWS(p.add_flag("-bad", "dash"));
  CHECK_THROWS(p.add_flag("a=b", "equals"));
  CHECK_THROWS(p.add_flag("a b", "space"));
  CHECK_THROWS(p.add_flag("help", "built in"));
  CHECK_THROWS(p.add_string("mod", "", "m", std::string("bpsk"), {"qpsk"}));   // default not among choices
  try {
    p.add_flag("quiet", "again");
  } catch (const UsageError&) {
    CHECK(false);   // must not be a UsageError
  } catch (const std::logic_error&) {
  }
}

TEST(reading_an_undeclared_or_mistyped_option_is_a_logic_error) {
  const ParseOutcome o = sample().parse({});
  CHECK_THROWS(o.values.str("nope"));
  CHECK_THROWS(o.values.given("nope"));
  CHECK_THROWS(o.values.str("sps"));          // an integer, not a string
  CHECK_THROWS(o.values.integer("rolloff"));  // a number, not an integer
  CHECK_THROWS(o.values.number("mod"));
  CHECK_THROWS(o.values.flag("sps"));
  CHECK_THROWS(o.values.integer("quiet"));
  try {
    o.values.str("nope");
  } catch (const UsageError&) {
    CHECK(false);
  } catch (const std::logic_error&) {
  }
}

// ---- Helpers ----------------------------------------------------------------

TEST(a_colon_triple_is_three_numbers) {
  const std::vector<double> t = parse_colon_triple("--sweep", "0:12:2");
  REQUIRE(t.size() == 3);
  CHECK_NEAR(t[0], 0.0, 0);
  CHECK_NEAR(t[1], 12.0, 0);
  CHECK_NEAR(t[2], 2.0, 0);
  const std::vector<double> u = parse_colon_triple("--sweep", "-3.5:1e1:0.25");
  REQUIRE(u.size() == 3);
  CHECK_NEAR(u[0], -3.5, 0);
  CHECK_NEAR(u[1], 10.0, 0);
  CHECK_NEAR(u[2], 0.25, 0);
}

TEST(a_malformed_colon_triple_is_refused_with_an_example) {
  for (const char* bad : {"", "5", "0:12", "0:12:2:1", "0::2", "a:b:c", "0:12:", ":12:2", "0:nan:2", "0:1e999:2"}) {
    try {
      parse_colon_triple("--sweep", bad);
      CHECK(false);
    } catch (const UsageError& e) {
      CHECK(contains(e.what(), "option '--sweep'"));
      CHECK(contains(e.what(), "START:STOP:STEP"));
    }
  }
}

TEST(the_closest_match_is_found_within_the_distance) {
  const std::vector<std::string> names = {"rolloff", "sps", "span", "seed"};
  CHECK(closest_match("rolof", names).value_or("") == "rolloff");
  CHECK(closest_match("sp", names).value_or("") == "sps");      // ties go to the first
  CHECK(closest_match("seeed", names).value_or("") == "seed");
  CHECK(!closest_match("completely-different", names).has_value());
  CHECK(!closest_match("rolof", names, 0).has_value());
  CHECK(closest_match("rolof", {}).has_value() == false);
  CHECK(closest_match("sps", names).value_or("") == "sps");     // exact
  CHECK(closest_match("see", {"seed", "sees", "seen"}).value_or("") == "seed");   // a tie goes to the first
}

MPSK_TEST_MAIN()
