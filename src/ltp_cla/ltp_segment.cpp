#include "ltp_cla/ltp_segment.h"
#include "ltp_cla/sdnv.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace ltp {

// ---------------------------------------------------------------------------
// Helper: append SDNV-encoded value to a byte vector
// ---------------------------------------------------------------------------
static void append_sdnv(std::vector<uint8_t>& out, uint64_t value) {
    auto encoded = sdnv::encode(value);
    out.insert(out.end(), encoded.begin(), encoded.end());
}

// ---------------------------------------------------------------------------
// Helper: encode an extension list
// ---------------------------------------------------------------------------
static void encode_extensions(std::vector<uint8_t>& out,
                              const std::vector<LtpExtension>& exts) {
    for (const auto& ext : exts) {
        out.push_back(ext.tag);
        append_sdnv(out, ext.value.size());
        out.insert(out.end(), ext.value.begin(), ext.value.end());
    }
}

// ---------------------------------------------------------------------------
// Helper: decode an extension list
// ---------------------------------------------------------------------------
static bool decode_extensions(const uint8_t* data, size_t length, size_t& offset,
                              uint8_t count, std::vector<LtpExtension>& exts) {
    exts.clear();
    exts.reserve(count);
    for (uint8_t i = 0; i < count; ++i) {
        if (offset >= length) return false;
        LtpExtension ext;
        ext.tag = data[offset++];
        uint64_t val_len = 0;
        try {
            val_len = sdnv::decode(data, length, offset);
        } catch (...) {
            return false;
        }
        if (offset + val_len > length) return false;
        ext.value.assign(data + offset, data + offset + val_len);
        offset += val_len;
        exts.push_back(std::move(ext));
    }
    return true;
}

// ---------------------------------------------------------------------------
// LtpSegment::encode()
// ---------------------------------------------------------------------------

std::vector<uint8_t> LtpSegment::encode() const {
    std::vector<uint8_t> out;
    out.reserve(64);  // reasonable initial capacity

    // Control byte: version (4 bits) | segment_type (4 bits)
    out.push_back(static_cast<uint8_t>((version << 4) | (segment_type & 0x0F)));

    // Session ID
    append_sdnv(out, engine_id);
    append_sdnv(out, session_number);

    // Extension counts
    out.push_back(header_ext_count);
    out.push_back(trailer_ext_count);

    // Header extensions
    encode_extensions(out, header_extensions);

    // Segment content (type-dependent)
    if (SegType::is_data(segment_type)) {
        const auto& ds = std::get<DataSegContent>(content);
        append_sdnv(out, ds.client_service_id);
        append_sdnv(out, ds.offset);
        append_sdnv(out, ds.length);
        if (SegType::has_checkpoint(segment_type)) {
            append_sdnv(out, ds.checkpoint_serial.value_or(0));
            append_sdnv(out, ds.report_serial.value_or(0));
        }
        out.insert(out.end(), ds.data.begin(), ds.data.end());

    } else if (segment_type == SegType::REPORT_SEGMENT) {
        const auto& rs = std::get<ReportSegContent>(content);
        append_sdnv(out, rs.report_serial);
        append_sdnv(out, rs.checkpoint_serial);
        append_sdnv(out, rs.upper_bound);
        append_sdnv(out, rs.lower_bound);
        append_sdnv(out, rs.claims.size());
        for (const auto& claim : rs.claims) {
            append_sdnv(out, claim.offset);
            append_sdnv(out, claim.length);
        }

    } else if (segment_type == SegType::REPORT_ACK) {
        const auto& ra = std::get<RptAckContent>(content);
        append_sdnv(out, ra.report_serial);

    } else if (segment_type == SegType::CANCEL_BY_SENDER ||
               segment_type == SegType::CANCEL_BY_RECEIVER) {
        const auto& cc = std::get<CancelContent>(content);
        out.push_back(cc.reason_code);

    }
    // Cancel-ack segments (types 9, 13) have no content body

    // Trailer extensions
    encode_extensions(out, trailer_extensions);

    return out;
}

// ---------------------------------------------------------------------------
// LtpSegment::decode()
// ---------------------------------------------------------------------------

std::optional<LtpSegment> LtpSegment::decode(const uint8_t* data, size_t length) {
    if (length == 0) {
        spdlog::debug("LTP segment decode: empty data");
        return std::nullopt;
    }

    size_t offset = 0;
    LtpSegment seg;

    // Control byte
    uint8_t control = data[offset++];
    seg.version = (control >> 4) & 0x0F;
    seg.segment_type = control & 0x0F;

    if (seg.version != 0) {
        spdlog::debug("LTP segment decode: invalid version {}", seg.version);
        return std::nullopt;
    }

    if (!SegType::is_valid(seg.segment_type)) {
        spdlog::debug("LTP segment decode: invalid segment type {}", seg.segment_type);
        return std::nullopt;
    }

    // Session ID
    try {
        seg.engine_id = sdnv::decode(data, length, offset);
        seg.session_number = sdnv::decode(data, length, offset);
    } catch (const std::runtime_error& e) {
        spdlog::debug("LTP segment decode: session ID SDNV error: {}", e.what());
        return std::nullopt;
    }

    // Extension counts
    if (offset + 2 > length) {
        spdlog::debug("LTP segment decode: truncated extension counts");
        return std::nullopt;
    }
    seg.header_ext_count = data[offset++];
    seg.trailer_ext_count = data[offset++];

    // Header extensions
    if (!decode_extensions(data, length, offset,
                           seg.header_ext_count, seg.header_extensions)) {
        spdlog::debug("LTP segment decode: malformed header extensions");
        return std::nullopt;
    }

    // Segment content
    try {
        if (SegType::is_data(seg.segment_type)) {
            DataSegContent ds;
            ds.client_service_id = sdnv::decode(data, length, offset);
            ds.offset = sdnv::decode(data, length, offset);
            ds.length = sdnv::decode(data, length, offset);
            if (SegType::has_checkpoint(seg.segment_type)) {
                ds.checkpoint_serial = sdnv::decode(data, length, offset);
                ds.report_serial = sdnv::decode(data, length, offset);
            }
            // Read data bytes
            if (offset + ds.length > length) {
                spdlog::debug("LTP segment decode: data segment truncated "
                              "(need {} bytes at offset {}, have {})",
                              ds.length, offset, length);
                return std::nullopt;
            }
            ds.data.assign(data + offset, data + offset + ds.length);
            offset += ds.length;
            seg.content = std::move(ds);

        } else if (seg.segment_type == SegType::REPORT_SEGMENT) {
            ReportSegContent rs;
            rs.report_serial = sdnv::decode(data, length, offset);
            rs.checkpoint_serial = sdnv::decode(data, length, offset);
            rs.upper_bound = sdnv::decode(data, length, offset);
            rs.lower_bound = sdnv::decode(data, length, offset);
            uint64_t claim_count = sdnv::decode(data, length, offset);
            rs.claims.reserve(static_cast<size_t>(claim_count));
            for (uint64_t i = 0; i < claim_count; ++i) {
                ReceptionClaim claim;
                claim.offset = sdnv::decode(data, length, offset);
                claim.length = sdnv::decode(data, length, offset);
                rs.claims.push_back(claim);
            }
            seg.content = std::move(rs);

        } else if (seg.segment_type == SegType::REPORT_ACK) {
            RptAckContent ra;
            ra.report_serial = sdnv::decode(data, length, offset);
            seg.content = std::move(ra);

        } else if (seg.segment_type == SegType::CANCEL_BY_SENDER ||
                   seg.segment_type == SegType::CANCEL_BY_RECEIVER) {
            if (offset >= length) {
                spdlog::debug("LTP segment decode: cancel segment missing reason code");
                return std::nullopt;
            }
            CancelContent cc;
            cc.reason_code = data[offset++];
            seg.content = std::move(cc);

        } else if (seg.segment_type == SegType::CANCEL_ACK_TO_SENDER ||
                   seg.segment_type == SegType::CANCEL_ACK_TO_RECEIVER) {
            // Cancel-ack segments have no content body.
            // Store as CancelContent with reason_code 0 (unused).
            seg.content = CancelContent{0};

        } else {
            spdlog::debug("LTP segment decode: unhandled segment type {}", seg.segment_type);
            return std::nullopt;
        }
    } catch (const std::runtime_error& e) {
        spdlog::debug("LTP segment decode: content SDNV error: {}", e.what());
        return std::nullopt;
    }

    // Trailer extensions
    if (!decode_extensions(data, length, offset,
                           seg.trailer_ext_count, seg.trailer_extensions)) {
        spdlog::debug("LTP segment decode: malformed trailer extensions");
        return std::nullopt;
    }

    return seg;
}

}  // namespace ltp
