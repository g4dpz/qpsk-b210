// Tests for cf32 sample files and payload sources. Requirements 2.1, 2.7, 3.8.

#include <cfloat>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "dsp/framer.hpp"
#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "io/payload_source.hpp"
#include "io/sample_file.hpp"
#include "temp_file.hpp"
#include "test_harness.hpp"

using namespace mpsk::io;
using cf = std::complex<float>;

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

/** True only if every sample is bit-for-bit identical, so -0.0 and NaN payloads count. */
bool bit_identical(const std::vector<cf>& a, const std::vector<cf>& b) {
  if (a.size() != b.size()) return false;
  return a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(cf)) == 0;
}

std::vector<uint8_t> read_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<cf> awkward_samples() {
  return {cf(0.0f, 0.0f),          cf(-0.0f, 0.0f),
          cf(1.0f, -1.0f),         cf(0.333333343f, -0.1f),
          cf(FLT_MIN, -FLT_MIN),   cf(FLT_MAX, -FLT_MAX),
          cf(FLT_TRUE_MIN, 1e-30f), cf(INFINITY, -INFINITY),
          cf(NAN, 0.5f),           cf(123456.789f, -0.000123f)};
}

}  // namespace

// ---- cf32 files -------------------------------------------------------------

TEST(samples_survive_a_write_and_read_bit_for_bit) {
  TempPath file(".cf32");
  const std::vector<cf> samples = awkward_samples();  // signed zero, denormals, infinities, NaN
  write_cf32(file.str(), samples);
  CHECK(bit_identical(read_cf32(file.str()), samples));
}

TEST(the_file_layout_is_little_endian_interleaved_iq) {
  TempPath file(".cf32");
  write_cf32(file.str(), {cf(1.0f, -2.0f), cf(0.5f, 0.0f)});

  // 1.0f = 0x3F800000, -2.0f = 0xC0000000, 0.5f = 0x3F000000, least significant byte first.
  const std::vector<uint8_t> expected{0x00, 0x00, 0x80, 0x3F,  // I0 = 1.0
                                      0x00, 0x00, 0x00, 0xC0,  // Q0 = -2.0
                                      0x00, 0x00, 0x00, 0x3F,  // I1 = 0.5
                                      0x00, 0x00, 0x00, 0x00}; // Q1 = 0.0
  CHECK(read_bytes(file.str()) == expected);
}

TEST(the_file_size_is_eight_bytes_per_sample) {
  TempPath file(".cf32");
  write_cf32(file.str(), std::vector<cf>(1234, cf(0.25f, -0.25f)));
  CHECK(file.size() == 1234 * 8);
}

TEST(an_empty_file_round_trips) {
  TempPath file(".cf32");
  write_cf32(file.str(), {});
  CHECK(file.size() == 0);
  CHECK(read_cf32(file.str()).empty());
}

TEST(a_large_file_spanning_many_internal_chunks_round_trips) {
  TempPath file(".cf32");
  std::vector<cf> samples(100000);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = cf(static_cast<float>(i) * 0.001f, -static_cast<float>(i));
  }
  write_cf32(file.str(), samples);
  CHECK(bit_identical(read_cf32(file.str()), samples));
}

TEST(a_streaming_writer_matches_a_one_shot_write) {
  TempPath whole_file(".cf32");
  TempPath chunked_file(".cf32");
  std::vector<cf> samples(10000);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    samples[i] = cf(static_cast<float>(i), static_cast<float>(-i) * 0.5f);
  }
  write_cf32(whole_file.str(), samples);

  Cf32Writer writer(chunked_file.str());
  std::size_t pos = 0;
  for (const std::size_t n : {1u, 7u, 4095u, 4096u, 4097u, 1u, 1803u}) {  // sums to 14000 > 10000
    const std::size_t take = std::min<std::size_t>(n, samples.size() - pos);
    writer.write(samples.data() + pos, take);
    pos += take;
  }
  writer.close();
  CHECK(writer.samples_written() == samples.size());
  CHECK(read_bytes(chunked_file.str()) == read_bytes(whole_file.str()));
}

TEST(append_mode_adds_to_the_end_and_the_default_replaces) {
  TempPath file(".cf32");
  write_cf32(file.str(), {cf(1.0f, 1.0f)});
  {
    Cf32Writer a(file.str(), /*append=*/true);
    a.write({cf(2.0f, 2.0f)});
    a.close();
  }
  CHECK(bit_identical(read_cf32(file.str()), {cf(1.0f, 1.0f), cf(2.0f, 2.0f)}));

  write_cf32(file.str(), {cf(9.0f, 9.0f)});  // replaces
  CHECK(bit_identical(read_cf32(file.str()), {cf(9.0f, 9.0f)}));
}

TEST(a_reader_returns_the_file_in_pieces_then_signals_the_end) {
  TempPath file(".cf32");
  std::vector<cf> samples(25);
  for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = cf(static_cast<float>(i), 0.0f);
  write_cf32(file.str(), samples);

  Cf32Reader reader(file.str());
  const auto first = reader.read(10);
  const auto second = reader.read(10);
  const auto third = reader.read(10);
  REQUIRE(first.size() == 10);
  REQUIRE(second.size() == 10);
  REQUIRE(third.size() == 5);        // the short final piece
  CHECK(reader.read(10).empty());    // then the end of the file
  CHECK(reader.read(10).empty());    // and it stays ended

  CHECK(first.front() == cf(0.0f, 0.0f));
  CHECK(second.front() == cf(10.0f, 0.0f));
  CHECK(third.back() == cf(24.0f, 0.0f));
}

TEST(reading_zero_samples_returns_nothing_and_does_not_move) {
  TempPath file(".cf32");
  write_cf32(file.str(), {cf(1.0f, 2.0f)});
  Cf32Reader reader(file.str());
  CHECK(reader.read(0).empty());
  CHECK(reader.read(5).size() == 1);
}

TEST(a_file_ending_mid_sample_is_reported_not_silently_cut) {
  TempPath file(".cf32");
  write_cf32(file.str(), {cf(1.0f, 2.0f), cf(3.0f, 4.0f)});
  std::vector<uint8_t> bytes = read_bytes(file.str());
  bytes.pop_back();  // 15 bytes: one whole sample and 7 stray ones
  write_bytes(file.str(), bytes);

  std::string message;
  try {
    read_cf32(file.str());
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(contains(message, "multiple of 8"));
  CHECK(contains(message, file.str()));
}

TEST(a_truncation_after_a_whole_chunk_is_still_detected) {
  // Chunks are 4096 samples internally. Cut inside the second chunk to make sure
  // the check applies on every read, not only the first.
  TempPath file(".cf32");
  write_cf32(file.str(), std::vector<cf>(5000, cf(1.0f, 1.0f)));
  std::vector<uint8_t> bytes = read_bytes(file.str());
  bytes.resize(bytes.size() - 3);
  write_bytes(file.str(), bytes);
  CHECK_THROWS(read_cf32(file.str()));
}

TEST(a_missing_file_is_reported_with_its_path) {
  TempPath file(".cf32");  // reserved but never created
  std::string message;
  try {
    read_cf32(file.str());
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(contains(message, file.str()));
  CHECK(contains(message, "cannot open"));
}

TEST(an_unwritable_location_is_reported_with_its_path) {
  const std::string bad = "/nonexistent_directory_for_mpsk_test/out.cf32";
  std::string message;
  try {
    write_cf32(bad, {cf(1.0f, 1.0f)});
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(contains(message, bad));
  CHECK(contains(message, "cannot open"));
}

TEST(closing_twice_is_harmless) {
  TempPath file(".cf32");
  Cf32Writer w(file.str());
  w.write({cf(1.0f, 1.0f)});
  w.close();
  w.close();
  CHECK(file.size() == 8);
}

TEST(a_modulated_burst_written_to_a_file_reads_back_exactly) {
  // The tx --out path (requirement 2.7) feeding the rx --in path (requirement 3.8).
  TempPath file(".cf32");
  const mpsk::dsp::Modulator m{mpsk::dsp::ModulatorConfig{}};
  const auto burst = m.modulate(mpsk::dsp::FrameType::Data, 21, mpsk::dsp::prbs_bytes(4, 21, 128));

  write_cf32(file.str(), burst.samples);
  CHECK(file.size() == static_cast<long>(burst.samples.size() * 8));
  CHECK(bit_identical(read_cf32(file.str()), burst.samples));
}

// ---- Payload sources --------------------------------------------------------

TEST(prbs_source_produces_the_regenerable_payload_for_each_frame) {
  PrbsPayloadSource source(42, 128);
  CHECK(source.regenerable());
  for (const uint16_t counter : {uint16_t{0}, uint16_t{1}, uint16_t{500}, uint16_t{65535}}) {
    const auto payload = source.next(counter);
    REQUIRE(payload.has_value());
    CHECK(payload->size() == 128);
    CHECK(*payload == mpsk::dsp::prbs_bytes(42, counter, 128));
  }
}

TEST(prbs_source_never_runs_out_and_differs_per_frame) {
  PrbsPayloadSource source(1, 16);
  const auto a = source.next(0);
  const auto b = source.next(1);
  REQUIRE(a.has_value() && b.has_value());
  CHECK(*a != *b);
  for (uint16_t i = 0; i < 1000; ++i) CHECK(source.next(i).has_value());
}

TEST(prbs_bytes_are_the_packed_prbs_bits) {
  const auto bytes = mpsk::dsp::prbs_bytes(7, 3, 33);
  CHECK(bytes.size() == 33);
  CHECK(mpsk::dsp::bytes_to_bits(bytes) == mpsk::dsp::prbs_bits(7, 3, 33 * 8));
  CHECK(mpsk::dsp::prbs_bytes(7, 3, 0).empty());
}

TEST(a_zero_size_prbs_payload_is_rejected) { CHECK_THROWS(PrbsPayloadSource(1, 0)); }

TEST(file_source_cuts_a_stream_into_chunks_with_a_short_last_one) {
  const std::string text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";  // 26 bytes
  FilePayloadSource source(std::make_unique<std::istringstream>(text), 10);
  CHECK(!source.regenerable());

  const auto a = source.next(0);
  const auto b = source.next(1);
  const auto c = source.next(2);
  const auto d = source.next(3);
  CHECK(!d.has_value());
  REQUIRE(a.has_value() && b.has_value() && c.has_value());
  REQUIRE(a->size() == 10 && b->size() == 10 && c->size() == 6);
  CHECK((*a)[0] == 'A' && (*b)[0] == 'K' && (*c)[0] == 'U' && c->back() == 'Z');
  CHECK(!source.next(4).has_value());  // and it stays empty
}

TEST(file_source_with_an_exact_multiple_has_no_empty_final_chunk) {
  FilePayloadSource source(std::make_unique<std::istringstream>(std::string(20, 'x')), 10);
  CHECK(source.next(0).has_value());
  CHECK(source.next(1).has_value());
  CHECK(!source.next(2).has_value());
}

TEST(file_source_on_an_empty_stream_has_nothing) {
  FilePayloadSource source(std::make_unique<std::istringstream>(""), 10);
  CHECK(!source.next(0).has_value());
}

TEST(file_source_preserves_arbitrary_binary_bytes) {
  std::string data;
  for (int i = 0; i < 256; ++i) data.push_back(static_cast<char>(i));  // includes NUL and 0xFF
  FilePayloadSource source(std::make_unique<std::istringstream>(data), 256);
  const auto chunk = source.next(0);
  REQUIRE(chunk.has_value());
  REQUIRE(chunk->size() == 256);
  for (int i = 0; i < 256; ++i) CHECK((*chunk)[static_cast<std::size_t>(i)] == i);
}

TEST(file_source_opens_a_real_file) {
  TempPath file(".bin");
  write_bytes(file.str(), {1, 2, 3, 4, 5});
  const auto source = FilePayloadSource::open(file.str(), 4);
  const auto a = source->next(0);
  const auto b = source->next(1);
  REQUIRE(a.has_value() && b.has_value());
  CHECK(*a == (std::vector<uint8_t>{1, 2, 3, 4}));
  CHECK(*b == (std::vector<uint8_t>{5}));
  CHECK(!source->next(2).has_value());
}

TEST(file_source_reports_a_missing_file_with_its_path) {
  TempPath file(".bin");
  std::string message;
  try {
    FilePayloadSource::open(file.str(), 4);
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(contains(message, file.str()));
}

TEST(file_source_rejects_bad_arguments) {
  CHECK_THROWS(FilePayloadSource(std::make_unique<std::istringstream>("abc"), 0));
  // An explicit empty unique_ptr, so the owning constructor is the one called. A
  // bare nullptr would select the private borrowed-stream overload instead.
  CHECK_THROWS(FilePayloadSource(std::unique_ptr<std::istream>(), 4));
}

TEST(file_payloads_decode_after_framing) {
  // A short file chunk (not a multiple of anything convenient) goes through
  // frame building and decoding with its length and content intact.
  const std::string text = "The quick brown fox jumps over the lazy dog.";
  FilePayloadSource source(std::make_unique<std::istringstream>(text), 64);
  const auto payload = source.next(0);
  REQUIRE(payload.has_value());
  CHECK(payload->size() == text.size());  // a 44-byte chunk, shorter than the 64 requested

  const auto symbols = mpsk::dsp::build_frame_symbols(mpsk::dsp::FrameType::Data, 4, 0, *payload);
  const mpsk::dsp::DecodedFrame d = mpsk::dsp::decode_frame_symbols(symbols);
  CHECK(d.header_ok);
  CHECK(d.payload_crc_ok);
  CHECK(std::string(d.payload.begin(), d.payload.end()) == text);
}

MPSK_TEST_MAIN()
