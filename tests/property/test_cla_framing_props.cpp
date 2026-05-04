// Feature: bp-ltp-dtn, Property 8: CLA Length-Prefix Framing Round-Trip
//
// For any valid byte sequence, framing with a 4-byte big-endian length prefix
// and then extracting SHALL produce a byte sequence identical to the original.
//
// **Validates: Requirement 9.7**

// Feature: bp-ltp-dtn, Property 9: TCP Ingress/Egress Framing Round-Trip
//
// Same framing is used for ingress/egress — same property applies.
//
// **Validates: Requirements 10.2, 11.7**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "ltp_cla/convergence_layer_adapter.h"

#include <cstdint>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Property 8: CLA Framing Round-Trip
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ClaFramingProperty8, FrameExtractRoundTrip, ()) {
    auto data = *rc::gen::container<std::vector<uint8_t>>(
        rc::gen::arbitrary<uint8_t>());
    // Limit size for test speed
    if (data.size() > 10000) data.resize(10000);

    auto framed = frame_message(data);

    std::vector<uint8_t> buffer(framed.begin(), framed.end());
    std::vector<uint8_t> extracted;

    RC_ASSERT(extract_framed_message(buffer, extracted));
    RC_ASSERT(extracted == data);
    RC_ASSERT(buffer.empty());
}

// ---------------------------------------------------------------------------
// Property 8b: Multiple messages in a stream
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ClaFramingProperty8, MultipleMessagesRoundTrip, ()) {
    // Generate 1-10 messages
    auto count = *rc::gen::inRange(1, 11);
    std::vector<std::vector<uint8_t>> messages;
    std::vector<uint8_t> stream;

    for (int i = 0; i < count; ++i) {
        auto msg = *rc::gen::container<std::vector<uint8_t>>(
            rc::gen::arbitrary<uint8_t>());
        if (msg.size() > 1000) msg.resize(1000);
        auto framed = frame_message(msg);
        stream.insert(stream.end(), framed.begin(), framed.end());
        messages.push_back(std::move(msg));
    }

    // Extract all messages
    std::vector<uint8_t> buffer(stream.begin(), stream.end());
    for (int i = 0; i < count; ++i) {
        std::vector<uint8_t> extracted;
        RC_ASSERT(extract_framed_message(buffer, extracted));
        RC_ASSERT(extracted == messages[static_cast<size_t>(i)]);
    }
    RC_ASSERT(buffer.empty());
}
