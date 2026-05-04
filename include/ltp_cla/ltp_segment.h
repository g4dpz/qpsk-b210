#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace ltp {

// ---- Segment type codes (4-bit field, RFC 5326 Section 3) ----

namespace SegType {
    constexpr uint8_t RED_DATA              = 0;   // Red data, NOT checkpoint, NOT EORP
    constexpr uint8_t RED_DATA_CP           = 1;   // Red data, checkpoint, NOT EORP
    constexpr uint8_t RED_DATA_CP_EORP      = 2;   // Red data, checkpoint, EORP
    constexpr uint8_t RED_DATA_CP_EORP_EOB  = 3;   // Red data, checkpoint, EORP, EOB
    constexpr uint8_t GREEN_DATA            = 4;   // Green data, NOT EOB
    constexpr uint8_t GREEN_DATA_EOB        = 5;   // Green data, EOB
    constexpr uint8_t REPORT_SEGMENT        = 6;
    constexpr uint8_t REPORT_ACK            = 7;
    constexpr uint8_t CANCEL_BY_SENDER      = 8;
    constexpr uint8_t CANCEL_ACK_TO_SENDER  = 9;
    constexpr uint8_t CANCEL_BY_RECEIVER    = 12;
    constexpr uint8_t CANCEL_ACK_TO_RECEIVER = 13;

    /// Returns true if the segment type code is a valid LTP segment type.
    inline bool is_valid(uint8_t type) {
        switch (type) {
            case 0: case 1: case 2: case 3:
            case 4: case 5: case 6: case 7:
            case 8: case 9: case 12: case 13:
                return true;
            default:
                return false;
        }
    }

    /// Returns true if the segment type is a data segment (red or green).
    inline bool is_data(uint8_t type) { return type <= 5; }

    /// Returns true if the segment type is a red data segment.
    inline bool is_red(uint8_t type) { return type <= 3; }

    /// Returns true if the segment type is a green data segment.
    inline bool is_green(uint8_t type) { return type >= 4 && type <= 5; }

    /// Returns true if the segment type has the checkpoint flag.
    inline bool has_checkpoint(uint8_t type) { return type >= 1 && type <= 3; }

    /// Returns true if the segment type has the EORP flag.
    inline bool has_eorp(uint8_t type) { return type == 2 || type == 3; }

    /// Returns true if the segment type has the EOB flag.
    inline bool has_eob(uint8_t type) { return type == 3 || type == 5; }
}  // namespace SegType

// ---- Extension ----

struct LtpExtension {
    uint8_t tag = 0;
    std::vector<uint8_t> value;

    bool operator==(const LtpExtension& other) const {
        return tag == other.tag && value == other.value;
    }
};

// ---- Reception claim ----

struct ReceptionClaim {
    uint64_t offset = 0;
    uint64_t length = 0;

    bool operator==(const ReceptionClaim& other) const {
        return offset == other.offset && length == other.length;
    }
};

// ---- Segment content types ----

struct DataSegContent {
    uint64_t client_service_id = 1;
    uint64_t offset = 0;
    uint64_t length = 0;
    std::optional<uint64_t> checkpoint_serial;
    std::optional<uint64_t> report_serial;
    std::vector<uint8_t> data;

    bool operator==(const DataSegContent& other) const {
        return client_service_id == other.client_service_id &&
               offset == other.offset &&
               length == other.length &&
               checkpoint_serial == other.checkpoint_serial &&
               report_serial == other.report_serial &&
               data == other.data;
    }
};

struct ReportSegContent {
    uint64_t report_serial = 0;
    uint64_t checkpoint_serial = 0;
    uint64_t upper_bound = 0;
    uint64_t lower_bound = 0;
    std::vector<ReceptionClaim> claims;

    bool operator==(const ReportSegContent& other) const {
        return report_serial == other.report_serial &&
               checkpoint_serial == other.checkpoint_serial &&
               upper_bound == other.upper_bound &&
               lower_bound == other.lower_bound &&
               claims == other.claims;
    }
};

struct RptAckContent {
    uint64_t report_serial = 0;

    bool operator==(const RptAckContent& other) const {
        return report_serial == other.report_serial;
    }
};

struct CancelContent {
    uint8_t reason_code = 0;

    bool operator==(const CancelContent& other) const {
        return reason_code == other.reason_code;
    }
};

using SegmentContent = std::variant<
    DataSegContent,
    ReportSegContent,
    RptAckContent,
    CancelContent
>;

// ---- LTP Segment ----

struct LtpSegment {
    uint8_t version = 0;
    uint8_t segment_type = 0;
    uint64_t engine_id = 0;
    uint64_t session_number = 0;
    uint8_t header_ext_count = 0;
    uint8_t trailer_ext_count = 0;
    std::vector<LtpExtension> header_extensions;
    std::vector<LtpExtension> trailer_extensions;
    SegmentContent content;

    /// Encode this segment to a byte sequence per RFC 5326 Section 3.
    std::vector<uint8_t> encode() const;

    /// Decode a segment from a byte sequence. Returns nullopt on malformed input.
    static std::optional<LtpSegment> decode(const uint8_t* data, size_t length);

    bool operator==(const LtpSegment& other) const {
        return version == other.version &&
               segment_type == other.segment_type &&
               engine_id == other.engine_id &&
               session_number == other.session_number &&
               header_ext_count == other.header_ext_count &&
               trailer_ext_count == other.trailer_ext_count &&
               header_extensions == other.header_extensions &&
               trailer_extensions == other.trailer_extensions &&
               content == other.content;
    }
};

}  // namespace ltp
