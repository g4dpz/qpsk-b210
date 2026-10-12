#pragma once

/**
 * @file test_harness.hpp
 * @brief Minimal assert-style test harness, with no third-party dependency.
 *
 * Usage in a test file:
 * @code
 *   #include "test_harness.hpp"
 *
 *   TEST(adds_numbers) {
 *     CHECK(1 + 1 == 2);
 *     CHECK_NEAR(0.1 + 0.2, 0.3, 1e-12);
 *   }
 *
 *   MPSK_TEST_MAIN()
 * @endcode
 *
 * A failed CHECK records the failure and lets the test continue, so one run
 * reports every failing check. The process exits non-zero if any check failed,
 * which is what CTest uses to decide pass or fail.
 */

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace mpsk_test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

/** Registry of test cases, filled by static initialisation of TEST(). */
inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

/** Number of failed checks in the test case currently running. */
inline int& failures_in_current() {
  static int n = 0;
  return n;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

inline void report_failure(const char* file, int line, const std::string& what) {
  std::fprintf(stderr, "    FAIL %s:%d: %s\n", file, line, what.c_str());
  ++failures_in_current();
}

/** Runs every registered test. @return 0 if all passed, 1 otherwise. */
inline int run_all() {
  int failed_tests = 0;
  for (const auto& t : registry()) {
    failures_in_current() = 0;
    std::fprintf(stdout, "[ RUN  ] %s\n", t.name.c_str());
    // An exception escaping a test is a failure of that test, not of the whole
    // run. Without this, one throwing test would terminate the process and hide
    // the result of every test after it.
    try {
      t.fn();
    } catch (const std::exception& e) {
      report_failure(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
    } catch (...) {
      report_failure(__FILE__, __LINE__, "uncaught non-standard exception");
    }
    if (failures_in_current() == 0) {
      std::fprintf(stdout, "[  OK  ] %s\n", t.name.c_str());
    } else {
      std::fprintf(stdout, "[ FAIL ] %s (%d failed checks)\n", t.name.c_str(),
                   failures_in_current());
      ++failed_tests;
    }
  }
  std::fprintf(stdout, "%zu tests, %d failed\n", registry().size(), failed_tests);
  return failed_tests == 0 ? 0 : 1;
}

}  // namespace mpsk_test

#define TEST(name)                                                      \
  static void test_##name();                                            \
  static ::mpsk_test::Registrar registrar_##name(#name, test_##name);   \
  static void test_##name()

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) ::mpsk_test::report_failure(__FILE__, __LINE__, #cond); \
  } while (0)

/**
 * Like CHECK, but a failure also ends the current test. Use it for a
 * precondition that later lines depend on, typically a container size checked
 * before indexing. A plain CHECK would record the failure and carry on to read
 * past the end, and that crashes the whole test binary (a segmentation fault
 * cannot be caught), hiding every result after it. Only usable directly in a
 * TEST body, which returns void.
 */
#define REQUIRE(cond)                                                       \
  do {                                                                      \
    if (!(cond)) {                                                          \
      ::mpsk_test::report_failure(__FILE__, __LINE__, "REQUIRE failed: " #cond); \
      return;                                                               \
    }                                                                       \
  } while (0)

/** Passes when |a - b| <= tol. NaN never passes. */
#define CHECK_NEAR(a, b, tol)                                           \
  do {                                                                  \
    const double mpsk_a = static_cast<double>(a);                       \
    const double mpsk_b = static_cast<double>(b);                       \
    const double mpsk_t = static_cast<double>(tol);                     \
    if (!(std::fabs(mpsk_a - mpsk_b) <= mpsk_t)) {                      \
      ::mpsk_test::report_failure(                                      \
          __FILE__, __LINE__,                                           \
          std::string(#a " ~= " #b " (got ") + std::to_string(mpsk_a) + \
              " vs " + std::to_string(mpsk_b) + ", tol " +              \
              std::to_string(mpsk_t) + ")");                            \
    }                                                                   \
  } while (0)

/** Passes when evaluating @p expr throws an exception derived from std::exception. */
#define CHECK_THROWS(expr)                                              \
  do {                                                                  \
    bool mpsk_threw = false;                                            \
    try {                                                               \
      (void)(expr);                                                     \
    } catch (const std::exception&) {                                  \
      mpsk_threw = true;                                                \
    }                                                                   \
    if (!mpsk_threw)                                                    \
      ::mpsk_test::report_failure(__FILE__, __LINE__,                   \
                                  "expected an exception from: " #expr); \
  } while (0)

#define MPSK_TEST_MAIN() \
  int main() { return ::mpsk_test::run_all(); }
