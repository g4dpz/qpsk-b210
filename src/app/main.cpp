#include <iostream>

#include "dsp/version.hpp"

#ifdef MPSK_WITH_UHD
#include "radio/uhd_info.hpp"
#endif

int main() {
  std::cout << "mpsk " << mpsk::version_string() << "\n";

#ifdef MPSK_WITH_UHD
  std::cout << "UHD radio layer: enabled\n"
            << "  UHD headers : " << mpsk::radio::uhd_compiled_version() << "\n"
            << "  UHD library : " << mpsk::radio::uhd_runtime_version() << "\n";

  if (!mpsk::radio::uhd_versions_match()) {
    std::cerr << "error: this build mixes UHD headers " << mpsk::radio::uhd_compiled_version()
              << " with UHD library " << mpsk::radio::uhd_runtime_version()
              << ".\nAn older UHD install is shadowing the intended one. "
                 "Rebuild with -DMPSK_UHD_PREFIX=<prefix> after removing or "
                 "hiding the stale headers.\n";
    return 1;
  }
#else
  std::cout << "UHD radio layer: not built (MPSK_WITH_UHD=OFF)\n";
#endif

  return 0;
}
