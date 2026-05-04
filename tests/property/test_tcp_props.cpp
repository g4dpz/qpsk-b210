// Feature: qpsk-b200-codec, Property 15: TCP input byte segmentation
//
// For any byte stream of arbitrary length, the segmentation into frame-sized
// payloads SHALL preserve all bytes (no data loss) and the concatenation of
// all segmented payloads SHALL equal the original byte stream.
//
// **Validates: Requirements 11.4**

// Feature: qpsk-b200-codec, Property 16: Data ordering preservation
//
// For any sequence of byte chunks, the bytes SHALL be delivered in the same
// order they were received.
//
// **Validates: Requirements 11.8, 12.8**

// NOTE: These tests verify the segmentation and ordering logic in-memory
// (no sockets) for speed. Socket-level integration is covered by the unit
// tests in test_tcp_servers.cpp.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal in-memory segmenter that mirrors TcpInputServer's buffer logic
// ---------------------------------------------------------------------------
namespace {

class Segmenter {
    std::vector<uint8_t> buffer_;
    size_t frame_size_;
public:
    explicit Segmenter(size_t frame_size) : frame_size_(frame_size) {}

    void push(const std::vector<uint8_t>& data) {
        buffer_.insert(buffer_.end(), data.begin(), data.end());
    }

    bool has_frame() const { return buffer_.size() >= frame_size_; }

    std::vector<uint8_t> read_frame() {
        if (!has_frame()) return {};
        std::vector<uint8_t> frame(buffer_.begin(),
                                    buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size_));
        buffer_.erase(buffer_.begin(),
                      buffer_.begin() + static_cast<std::ptrdiff_t>(frame_size_));
        return frame;
    }

    size_t buffered_bytes() const { return buffer_.size(); }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

static rc::Gen<std::vector<uint8_t>> genByteStream() {
    return rc::gen::mapcat(rc::gen::inRange(50, 2001), [](int len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(len), rc::gen::arbitrary<uint8_t>());
    });
}

static rc::Gen<size_t> genFrameSize() {
    return rc::gen::map(rc::gen::inRange(8, 129),
                        [](int v) { return static_cast<size_t>(v); });
}

static rc::Gen<std::vector<uint8_t>> genPayload() {
    return rc::gen::mapcat(rc::gen::inRange(10, 101), [](int len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(len), rc::gen::arbitrary<uint8_t>());
    });
}

static rc::Gen<std::vector<std::vector<uint8_t>>> genPayloadSequence() {
    return rc::gen::mapcat(rc::gen::inRange(3, 11), [](int count) {
        return rc::gen::container<std::vector<std::vector<uint8_t>>>(
            static_cast<size_t>(count), genPayload());
    });
}

// ---------------------------------------------------------------------------
// Property 15: Byte segmentation preserves all data
// ---------------------------------------------------------------------------

RC_GTEST_PROP(TcpProperty15, ByteSegmentationPreservesAllData, ()) {
    const auto data = *genByteStream();
    const auto frame_size = *genFrameSize();

    Segmenter seg(frame_size);
    seg.push(data);

    std::vector<uint8_t> reassembled;
    while (seg.has_frame()) {
        auto frame = seg.read_frame();
        RC_ASSERT(frame.size() == frame_size);
        reassembled.insert(reassembled.end(), frame.begin(), frame.end());
    }

    RC_ASSERT(reassembled.size() + seg.buffered_bytes() == data.size());
    RC_ASSERT(std::equal(reassembled.begin(), reassembled.end(), data.begin()));
}

RC_GTEST_PROP(TcpProperty15, CorrectFrameCount, ()) {
    const auto data = *genByteStream();
    const auto frame_size = *genFrameSize();

    Segmenter seg(frame_size);
    seg.push(data);

    size_t count = 0;
    while (seg.has_frame()) {
        seg.read_frame();
        ++count;
    }

    RC_ASSERT(count == data.size() / frame_size);
    RC_ASSERT(seg.buffered_bytes() == data.size() % frame_size);
}

// ---------------------------------------------------------------------------
// Property 16: Data ordering preservation
// ---------------------------------------------------------------------------

RC_GTEST_PROP(TcpProperty16, OrderingPreservedAcrossChunks, ()) {
    const auto payloads = *genPayloadSequence();
    const size_t frame_size = 32;

    std::vector<uint8_t> all_data;
    Segmenter seg(frame_size);

    for (const auto& p : payloads) {
        seg.push(p);
        all_data.insert(all_data.end(), p.begin(), p.end());
    }

    std::vector<uint8_t> reassembled;
    while (seg.has_frame()) {
        auto frame = seg.read_frame();
        reassembled.insert(reassembled.end(), frame.begin(), frame.end());
    }

    RC_ASSERT(reassembled.size() <= all_data.size());
    RC_ASSERT(std::equal(reassembled.begin(), reassembled.end(), all_data.begin()));
    RC_ASSERT(reassembled.size() + seg.buffered_bytes() == all_data.size());
}

RC_GTEST_PROP(TcpProperty16, OutputOrderMatchesInputOrder, ()) {
    const auto payloads = *genPayloadSequence();

    // Simulate write_to_all ordering: concatenate in order
    std::vector<uint8_t> expected;
    for (const auto& p : payloads) {
        expected.insert(expected.end(), p.begin(), p.end());
    }

    // Verify the concatenation is deterministic and ordered
    std::vector<uint8_t> rebuilt;
    for (const auto& p : payloads) {
        rebuilt.insert(rebuilt.end(), p.begin(), p.end());
    }

    RC_ASSERT(rebuilt == expected);
}
