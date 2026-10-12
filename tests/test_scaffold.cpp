// Placeholder test for task 1. It checks that the harness works (including that
// a failing check really is detected) and that the library link graph is wired.
// Real DSP tests replace this coverage as the blocks arrive.

#include <cstring>

#include "dsp/version.hpp"
#include "link/link_info.hpp"
#include "sim/sim_info.hpp"
#include "test_harness.hpp"

TEST(harness_check_passes_on_true) {
  CHECK(true);
  CHECK_NEAR(0.1 + 0.2, 0.3, 1e-12);
}

TEST(harness_counts_failures) {
  // Run a failing check inside this test, then clear the count so the test
  // itself passes. This proves a failing CHECK is registered, not ignored.
  const int before = mpsk_test::failures_in_current();
  CHECK(false);
  CHECK_NEAR(1.0, 2.0, 0.1);
  const int after = mpsk_test::failures_in_current();
  mpsk_test::failures_in_current() = before;
  CHECK(after - before == 2);
}

TEST(harness_check_near_rejects_nan) {
  const int before = mpsk_test::failures_in_current();
  CHECK_NEAR(std::nan(""), 0.0, 1.0);
  const int after = mpsk_test::failures_in_current();
  mpsk_test::failures_in_current() = before;
  CHECK(after - before == 1);
}

TEST(version_is_set) {
  CHECK(std::strlen(mpsk::version_string()) > 0);
}

TEST(layers_link_together) {
  CHECK(std::strcmp(mpsk::dsp::layer_name(), "dsp") == 0);
  CHECK(std::strcmp(mpsk::sim::layer_name(), "sim") == 0);
  CHECK(std::strcmp(mpsk::link::layer_name(), "link") == 0);
}

MPSK_TEST_MAIN()
