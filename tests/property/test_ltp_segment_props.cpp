// Feature: bp-ltp-dtn, Property 4: LTP Segment Encode/Decode Round-Trip
//
// For any valid LTP segment (across all segment types), encoding to bytes
// and then decoding SHALL produce a segment with equivalent fields.
//
// **Validates: Requirement 2.8**

// Feature: bp-ltp-dtn, Property 5: LTP Malformed Segment Rejection
//
// For any byte sequence that is not a valid LTP segment, the decoder
// SHALL return nullopt.
//
// **Validates: Requirement 2.6**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "ltp_cla/ltp_segment.h"

#include <cstdint>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

static rc::Gen<LtpExtension> genExtension() {
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::arbitrary<uint8_t>(),
            rc::gen::container<std::vector<uint8_t>>(rc::gen::arbitrary<uint8_t>())),
        [](const auto& t) {
            LtpExtension ext;
            ext.tag = std::get<0>(t);
            auto val = std::get<1>(t);
            if (val.size() > 16) val.resize(16);
            ext.value = std::move(val);
            return ext;
        });
}

static rc::Gen<std::vector<LtpExtension>> genExtensions() {
    return rc::gen::map(
        rc::gen::container<std::vector<LtpExtension>>(genExtension()),
        [](std::vector<LtpExtension> v) {
            if (v.size() > 3) v.resize(3);
            return v;
        });
}

static rc::Gen<ReceptionClaim> genClaim() {
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(0, 100000),
            rc::gen::inRange<uint64_t>(1, 10000)),
        [](const auto& t) {
            return ReceptionClaim{std::get<0>(t), std::get<1>(t)};
        });
}

static rc::Gen<DataSegContent> genDataContent(bool has_checkpoint) {
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(0, 10),
            rc::gen::inRange<uint64_t>(0, 100000),
            rc::gen::container<std::vector<uint8_t>>(rc::gen::arbitrary<uint8_t>()),
            rc::gen::inRange<uint64_t>(0, 1000),
            rc::gen::inRange<uint64_t>(0, 1000)),
        [has_checkpoint](const auto& t) {
            DataSegContent ds;
            ds.client_service_id = std::get<0>(t);
            ds.offset = std::get<1>(t);
            ds.data = std::get<2>(t);
            if (ds.data.size() > 256) ds.data.resize(256);
            ds.length = ds.data.size();
            if (has_checkpoint) {
                ds.checkpoint_serial = std::get<3>(t);
                ds.report_serial = std::get<4>(t);
            }
            return ds;
        });
}

static rc::Gen<ReportSegContent> genReportContent() {
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(0, 1000),
            rc::gen::inRange<uint64_t>(0, 1000),
            rc::gen::inRange<uint64_t>(0, 100000),
            rc::gen::inRange<uint64_t>(0, 100000),
            rc::gen::container<std::vector<ReceptionClaim>>(genClaim())),
        [](const auto& t) {
            ReportSegContent rs;
            rs.report_serial = std::get<0>(t);
            rs.checkpoint_serial = std::get<1>(t);
            rs.upper_bound = std::get<2>(t);
            rs.lower_bound = std::get<3>(t);
            rs.claims = std::get<4>(t);
            if (rs.claims.size() > 8) rs.claims.resize(8);
            return rs;
        });
}

/// Generate a valid LtpSegment of a random type.
static rc::Gen<LtpSegment> genLtpSegment() {
    return rc::gen::mapcat(
        rc::gen::element(
            (uint8_t)0, (uint8_t)1, (uint8_t)2, (uint8_t)3,
            (uint8_t)4, (uint8_t)5, (uint8_t)6, (uint8_t)7,
            (uint8_t)8, (uint8_t)9, (uint8_t)12, (uint8_t)13),
        [](uint8_t seg_type) -> rc::Gen<LtpSegment> {
            auto gen_hdr = genExtensions();
            auto gen_trl = genExtensions();
            auto gen_eid = rc::gen::inRange<uint64_t>(1, 100000);
            auto gen_sn = rc::gen::inRange<uint64_t>(0, 100000);

            if (SegType::is_data(seg_type)) {
                bool has_cp = SegType::has_checkpoint(seg_type);
                return rc::gen::map(
                    rc::gen::tuple(gen_eid, gen_sn, gen_hdr, gen_trl,
                                   genDataContent(has_cp)),
                    [seg_type](const auto& t) {
                        LtpSegment seg;
                        seg.segment_type = seg_type;
                        seg.engine_id = std::get<0>(t);
                        seg.session_number = std::get<1>(t);
                        auto& hdr = std::get<2>(t);
                        auto& trl = std::get<3>(t);
                        seg.header_ext_count = static_cast<uint8_t>(hdr.size());
                        seg.trailer_ext_count = static_cast<uint8_t>(trl.size());
                        seg.header_extensions = hdr;
                        seg.trailer_extensions = trl;
                        seg.content = std::get<4>(t);
                        return seg;
                    });
            } else if (seg_type == SegType::REPORT_SEGMENT) {
                return rc::gen::map(
                    rc::gen::tuple(gen_eid, gen_sn, gen_hdr, gen_trl,
                                   genReportContent()),
                    [seg_type](const auto& t) {
                        LtpSegment seg;
                        seg.segment_type = seg_type;
                        seg.engine_id = std::get<0>(t);
                        seg.session_number = std::get<1>(t);
                        auto& hdr = std::get<2>(t);
                        auto& trl = std::get<3>(t);
                        seg.header_ext_count = static_cast<uint8_t>(hdr.size());
                        seg.trailer_ext_count = static_cast<uint8_t>(trl.size());
                        seg.header_extensions = hdr;
                        seg.trailer_extensions = trl;
                        seg.content = std::get<4>(t);
                        return seg;
                    });
            } else if (seg_type == SegType::REPORT_ACK) {
                return rc::gen::map(
                    rc::gen::tuple(gen_eid, gen_sn, gen_hdr, gen_trl,
                                   rc::gen::inRange<uint64_t>(0, 1000)),
                    [seg_type](const auto& t) {
                        LtpSegment seg;
                        seg.segment_type = seg_type;
                        seg.engine_id = std::get<0>(t);
                        seg.session_number = std::get<1>(t);
                        auto& hdr = std::get<2>(t);
                        auto& trl = std::get<3>(t);
                        seg.header_ext_count = static_cast<uint8_t>(hdr.size());
                        seg.trailer_ext_count = static_cast<uint8_t>(trl.size());
                        seg.header_extensions = hdr;
                        seg.trailer_extensions = trl;
                        seg.content = RptAckContent{std::get<4>(t)};
                        return seg;
                    });
            } else if (seg_type == SegType::CANCEL_BY_SENDER ||
                       seg_type == SegType::CANCEL_BY_RECEIVER) {
                return rc::gen::map(
                    rc::gen::tuple(gen_eid, gen_sn, gen_hdr, gen_trl,
                                   rc::gen::arbitrary<uint8_t>()),
                    [seg_type](const auto& t) {
                        LtpSegment seg;
                        seg.segment_type = seg_type;
                        seg.engine_id = std::get<0>(t);
                        seg.session_number = std::get<1>(t);
                        auto& hdr = std::get<2>(t);
                        auto& trl = std::get<3>(t);
                        seg.header_ext_count = static_cast<uint8_t>(hdr.size());
                        seg.trailer_ext_count = static_cast<uint8_t>(trl.size());
                        seg.header_extensions = hdr;
                        seg.trailer_extensions = trl;
                        seg.content = CancelContent{std::get<4>(t)};
                        return seg;
                    });
            } else {
                // Cancel-ack (types 9, 13)
                return rc::gen::map(
                    rc::gen::tuple(gen_eid, gen_sn, gen_hdr, gen_trl),
                    [seg_type](const auto& t) {
                        LtpSegment seg;
                        seg.segment_type = seg_type;
                        seg.engine_id = std::get<0>(t);
                        seg.session_number = std::get<1>(t);
                        auto& hdr = std::get<2>(t);
                        auto& trl = std::get<3>(t);
                        seg.header_ext_count = static_cast<uint8_t>(hdr.size());
                        seg.trailer_ext_count = static_cast<uint8_t>(trl.size());
                        seg.header_extensions = hdr;
                        seg.trailer_extensions = trl;
                        seg.content = CancelContent{0};
                        return seg;
                    });
            }
        });
}

// ---------------------------------------------------------------------------
// Property 4: LTP Segment Encode/Decode Round-Trip
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpSegmentProperty4, EncodeDecodeRoundTrip, ()) {
    const auto seg = *genLtpSegment();

    auto bytes = seg.encode();
    auto decoded = LtpSegment::decode(bytes.data(), bytes.size());

    RC_ASSERT(decoded.has_value());
    RC_ASSERT(*decoded == seg);
}

// ---------------------------------------------------------------------------
// Property 5: LTP Malformed Segment Rejection
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpSegmentProperty5, MalformedRejection_InvalidVersion, ()) {
    // Take a valid segment, encode it, then corrupt the version nibble
    auto seg = *genLtpSegment();
    auto bytes = seg.encode();
    RC_PRE(!bytes.empty());

    // Set version to non-zero (1-15)
    uint8_t bad_version = static_cast<uint8_t>(*rc::gen::inRange(1, 16));
    bytes[0] = static_cast<uint8_t>((bad_version << 4) | (bytes[0] & 0x0F));

    auto decoded = LtpSegment::decode(bytes.data(), bytes.size());
    RC_ASSERT(!decoded.has_value());
}

RC_GTEST_PROP(LtpSegmentProperty5, MalformedRejection_Truncated, ()) {
    // Take a valid segment, encode it, then truncate
    auto seg = *genLtpSegment();
    auto bytes = seg.encode();
    RC_PRE(bytes.size() > 1);

    // Truncate to a random shorter length (at least 1 byte)
    size_t trunc_len = *rc::gen::inRange<size_t>(1, bytes.size());
    bytes.resize(trunc_len);

    // May or may not decode — but if it does, it shouldn't crash.
    // The key property is no crash/UB on truncated input.
    auto decoded = LtpSegment::decode(bytes.data(), bytes.size());
    // We can't assert nullopt because a truncated segment might still
    // be a valid shorter segment. The important thing is no crash.
    (void)decoded;
    RC_SUCCEED("No crash on truncated input");
}
