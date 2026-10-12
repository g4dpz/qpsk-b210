// Guards against building with one UHD install's headers and linking another's
// library. See the comment in src/radio/CMakeLists.txt for how that happens on
// a machine that has both /opt/homebrew and /usr/local copies of UHD.

#include <iostream>

#include "radio/uhd_info.hpp"
#include "test_harness.hpp"

TEST(uhd_headers_match_library) {
  std::cout << "    headers=" << mpsk::radio::uhd_compiled_version()
            << " library=" << mpsk::radio::uhd_runtime_version() << "\n";
  CHECK(mpsk::radio::uhd_versions_match());
}

TEST(uhd_versions_are_not_empty) {
  CHECK(!mpsk::radio::uhd_compiled_version().empty());
  CHECK(!mpsk::radio::uhd_runtime_version().empty());
}

MPSK_TEST_MAIN()
