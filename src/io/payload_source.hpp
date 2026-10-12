#pragma once

/**
 * @file payload_source.hpp
 * @brief Where the transmitter's payload bytes come from (requirement 2.1).
 *
 * Either a seeded pseudorandom generator or an input file or stdin.
 */

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mpsk::io {

class PayloadSource {
 public:
  virtual ~PayloadSource() = default;

  /**
   * Payload for the frame with this counter, or std::nullopt when there is
   * nothing left to send.
   */
  virtual std::optional<std::vector<uint8_t>> next(uint16_t frame_counter) = 0;

  /**
   * True if a receiver that knows the seed can rebuild the expected payload of
   * any frame from its counter alone, which is what lets it measure bit error
   * rate without the data being sent separately. True for PRBS, false for a file.
   */
  virtual bool regenerable() const = 0;
};

/** Endless seeded pseudorandom payloads of a fixed size. */
class PrbsPayloadSource : public PayloadSource {
 public:
  /** @throws std::invalid_argument if @p payload_bytes is zero. */
  PrbsPayloadSource(uint32_t seed, std::size_t payload_bytes);

  std::optional<std::vector<uint8_t>> next(uint16_t frame_counter) override;
  bool regenerable() const override { return true; }

 private:
  uint32_t seed_;
  std::size_t payload_bytes_;
};

/**
 * Payloads cut from a byte stream, @p chunk_bytes at a time. The last chunk may
 * be shorter. After the end of the stream, next() returns std::nullopt.
 */
class FilePayloadSource : public PayloadSource {
 public:
  /**
   * @param in  The stream to read. Ownership is taken, so pass an ifstream.
   * @throws std::invalid_argument if @p chunk_bytes is zero.
   */
  FilePayloadSource(std::unique_ptr<std::istream> in, std::size_t chunk_bytes);

  /**
   * Opens @p path for reading, or reads standard input when @p path is "-".
   * @throws std::runtime_error if the file cannot be opened.
   */
  static std::unique_ptr<FilePayloadSource> open(const std::string& path, std::size_t chunk_bytes);

  std::optional<std::vector<uint8_t>> next(uint16_t frame_counter) override;
  bool regenerable() const override { return false; }

 private:
  FilePayloadSource(std::istream* borrowed, std::size_t chunk_bytes);

  std::unique_ptr<std::istream> owned_;
  std::istream* in_;
  std::size_t chunk_bytes_;
};

}  // namespace mpsk::io
