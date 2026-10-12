#pragma once

/**
 * @file temp_file.hpp
 * @brief A unique temporary file path that is removed when it goes out of scope.
 *
 * The path is not created, only reserved, so a test decides whether and how to
 * create it. Any file left at the path is deleted on destruction, including when
 * a test fails part way.
 */

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

#include <unistd.h>

class TempPath {
 public:
  explicit TempPath(const std::string& suffix = ".tmp") {
    static std::atomic<unsigned> counter{0};
    path_ = (std::filesystem::temp_directory_path() /
             ("mpsk_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + suffix))
                .string();
  }

  ~TempPath() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  TempPath(const TempPath&) = delete;
  TempPath& operator=(const TempPath&) = delete;

  const std::string& str() const { return path_; }

  /** Size in bytes of the file at the path, or -1 if there is none. */
  long size() const {
    std::error_code ec;
    const auto n = std::filesystem::file_size(path_, ec);
    return ec ? -1 : static_cast<long>(n);
  }

 private:
  std::string path_;
};
