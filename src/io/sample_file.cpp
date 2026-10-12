#include "io/sample_file.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace mpsk::io {

namespace {

/** Samples converted per buffer, so a long write needs no huge temporary. */
constexpr std::size_t kChunkSamples = 4096;

/** Stores a float as four little-endian bytes, whatever the host byte order. */
void store_le(float value, uint8_t* out) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  out[0] = static_cast<uint8_t>(bits & 0xFF);
  out[1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((bits >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((bits >> 24) & 0xFF);
}

float load_le(const uint8_t* in) {
  const uint32_t bits = static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
                        (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

}  // namespace

Cf32Writer::Cf32Writer(const std::string& path, bool append) : path_(path) {
  const auto mode = std::ios::binary | std::ios::out | (append ? std::ios::app : std::ios::trunc);
  out_.open(path, mode);
  if (!out_) {
    throw std::runtime_error("cannot open '" + path + "' for writing: " + std::strerror(errno));
  }
}

Cf32Writer::~Cf32Writer() {
  // A destructor must not throw, so errors here are dropped. close() reports them.
  if (out_.is_open()) out_.close();
}

void Cf32Writer::write(const std::complex<float>* samples, std::size_t count) {
  std::vector<uint8_t> buffer(kChunkSamples * kCf32BytesPerSample);
  std::size_t done = 0;
  while (done < count) {
    const std::size_t n = std::min(kChunkSamples, count - done);
    for (std::size_t i = 0; i < n; ++i) {
      store_le(samples[done + i].real(), &buffer[i * kCf32BytesPerSample]);
      store_le(samples[done + i].imag(), &buffer[i * kCf32BytesPerSample + 4]);
    }
    out_.write(reinterpret_cast<const char*>(buffer.data()),
               static_cast<std::streamsize>(n * kCf32BytesPerSample));
    if (!out_) throw std::runtime_error("write to '" + path_ + "' failed (disk full?)");
    done += n;
  }
  samples_written_ += count;
}

void Cf32Writer::close() {
  if (!out_.is_open()) return;
  out_.flush();
  const bool ok = static_cast<bool>(out_);
  out_.close();
  if (!ok) throw std::runtime_error("write to '" + path_ + "' failed (disk full?)");
}

Cf32Reader::Cf32Reader(const std::string& path) : path_(path) {
  in_.open(path, std::ios::binary | std::ios::in);
  if (!in_) {
    throw std::runtime_error("cannot open '" + path + "' for reading: " + std::strerror(errno));
  }
}

std::vector<std::complex<float>> Cf32Reader::read(std::size_t max_samples) {
  std::vector<std::complex<float>> samples;
  std::vector<uint8_t> buffer(kChunkSamples * kCf32BytesPerSample);

  while (samples.size() < max_samples) {
    const std::size_t want = std::min(kChunkSamples, max_samples - samples.size());
    in_.read(reinterpret_cast<char*>(buffer.data()),
             static_cast<std::streamsize>(want * kCf32BytesPerSample));
    const std::size_t got_bytes = static_cast<std::size_t>(in_.gcount());

    if (got_bytes % kCf32BytesPerSample != 0) {
      throw std::runtime_error("'" + path_ + "' ends in the middle of a sample (" +
                               std::to_string(got_bytes % kCf32BytesPerSample) +
                               " stray bytes); its size is not a multiple of 8 bytes, so it is "
                               "truncated or not a cf32 file");
    }
    const std::size_t got = got_bytes / kCf32BytesPerSample;
    for (std::size_t i = 0; i < got; ++i) {
      samples.emplace_back(load_le(&buffer[i * kCf32BytesPerSample]),
                           load_le(&buffer[i * kCf32BytesPerSample + 4]));
    }
    if (got < want) break;  // end of file
  }
  return samples;
}

void write_cf32(const std::string& path, const std::vector<std::complex<float>>& samples) {
  Cf32Writer writer(path);
  writer.write(samples);
  writer.close();
}

std::vector<std::complex<float>> read_cf32(const std::string& path) {
  Cf32Reader reader(path);
  std::vector<std::complex<float>> all;
  for (;;) {
    std::vector<std::complex<float>> chunk = reader.read(1u << 16);
    if (chunk.empty()) break;
    all.insert(all.end(), chunk.begin(), chunk.end());
  }
  return all;
}

}  // namespace mpsk::io
