#pragma once

/**
 * @file sample_file.hpp
 * @brief Reading and writing baseband sample files in "cf32" format.
 *
 * cf32 is raw interleaved little-endian IEEE-754 float32 I/Q pairs:
 *
 *   I0 Q0 I1 Q1 ...      8 bytes per complex sample, no header
 *
 * It is the layout of UHD's rx_samples_to_file with "--type float" and of GNU
 * Radio's file sink for complex floats, so files move between those tools and
 * this program unchanged. Byte order is fixed to little-endian by explicit
 * conversion, so a file written on one machine reads identically on another.
 *
 * Failures (a path that cannot be opened, a write that runs out of disk, a file
 * whose size is not a whole number of samples) throw std::runtime_error with the
 * path in the message.
 */

#include <complex>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

namespace mpsk::io {

constexpr std::size_t kCf32BytesPerSample = 8;

/** Streaming writer. Create one, call write() as often as needed, then close(). */
class Cf32Writer {
 public:
  /**
   * @param append  If true, samples are added to the end of an existing file
   *                instead of replacing it.
   * @throws std::runtime_error if the file cannot be opened for writing.
   */
  explicit Cf32Writer(const std::string& path, bool append = false);

  /** Flushes and closes quietly. Call close() first if you need to see write errors. */
  ~Cf32Writer();

  Cf32Writer(const Cf32Writer&) = delete;
  Cf32Writer& operator=(const Cf32Writer&) = delete;

  /** @throws std::runtime_error if the write fails, for example a full disk. */
  void write(const std::complex<float>* samples, std::size_t count);
  void write(const std::vector<std::complex<float>>& samples) {
    write(samples.data(), samples.size());
  }

  /** Flushes and closes, reporting any error. Safe to call more than once. */
  void close();

  std::size_t samples_written() const { return samples_written_; }

 private:
  std::string path_;
  std::ofstream out_;
  std::size_t samples_written_ = 0;
};

/** Streaming reader. */
class Cf32Reader {
 public:
  /** @throws std::runtime_error if the file cannot be opened. */
  explicit Cf32Reader(const std::string& path);

  /**
   * Reads up to @p max_samples samples. An empty result means the end of the file.
   * @throws std::runtime_error if the file ends in the middle of a sample, which
   *         means it was truncated or is not a cf32 file.
   */
  std::vector<std::complex<float>> read(std::size_t max_samples);

 private:
  std::string path_;
  std::ifstream in_;
};

/** Writes a whole file, replacing any existing one. */
void write_cf32(const std::string& path, const std::vector<std::complex<float>>& samples);

/** Reads a whole file. */
std::vector<std::complex<float>> read_cf32(const std::string& path);

}  // namespace mpsk::io
