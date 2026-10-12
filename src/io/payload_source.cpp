#include "io/payload_source.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "dsp/prbs.hpp"

namespace mpsk::io {

PrbsPayloadSource::PrbsPayloadSource(uint32_t seed, std::size_t payload_bytes)
    : seed_(seed), payload_bytes_(payload_bytes) {
  if (payload_bytes == 0) {
    throw std::invalid_argument("PrbsPayloadSource: payload size must be at least 1 byte");
  }
}

std::optional<std::vector<uint8_t>> PrbsPayloadSource::next(uint16_t frame_counter) {
  return mpsk::dsp::prbs_bytes(seed_, frame_counter, payload_bytes_);
}

FilePayloadSource::FilePayloadSource(std::unique_ptr<std::istream> in, std::size_t chunk_bytes)
    : owned_(std::move(in)), in_(owned_.get()), chunk_bytes_(chunk_bytes) {
  if (chunk_bytes == 0) {
    throw std::invalid_argument("FilePayloadSource: chunk size must be at least 1 byte");
  }
  if (in_ == nullptr) throw std::invalid_argument("FilePayloadSource: null input stream");
}

FilePayloadSource::FilePayloadSource(std::istream* borrowed, std::size_t chunk_bytes)
    : in_(borrowed), chunk_bytes_(chunk_bytes) {
  if (chunk_bytes == 0) {
    throw std::invalid_argument("FilePayloadSource: chunk size must be at least 1 byte");
  }
}

std::unique_ptr<FilePayloadSource> FilePayloadSource::open(const std::string& path,
                                                           std::size_t chunk_bytes) {
  if (path == "-") {
    // Standard input is borrowed, not owned: the stream must outlive this source
    // and must not be closed by it.
    return std::unique_ptr<FilePayloadSource>(new FilePayloadSource(&std::cin, chunk_bytes));
  }
  auto file = std::make_unique<std::ifstream>(path, std::ios::binary | std::ios::in);
  if (!*file) {
    throw std::runtime_error("cannot open payload file '" + path + "': " + std::strerror(errno));
  }
  return std::make_unique<FilePayloadSource>(std::move(file), chunk_bytes);
}

std::optional<std::vector<uint8_t>> FilePayloadSource::next(uint16_t /*frame_counter*/) {
  std::vector<uint8_t> chunk(chunk_bytes_);
  in_->read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk_bytes_));
  const std::size_t got = static_cast<std::size_t>(in_->gcount());
  if (got == 0) return std::nullopt;
  chunk.resize(got);
  return chunk;
}

}  // namespace mpsk::io
