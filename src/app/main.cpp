#include <iostream>
#include <string>
#include <vector>

#include "app/cli.hpp"

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  return mpsk::app::run_cli(args, std::cout, std::cerr);
}
