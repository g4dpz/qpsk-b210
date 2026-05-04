#pragma once

#include "ltp_cla/reception_tracker.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace ltp {

enum class SessionRole : uint8_t { ORIGINATOR, RECEIVER };
enum class SessionState : uint8_t { ACTIVE, COMPLETED, CANCELLED };

/// Tracks the state of a single LTP session.
struct LtpSession {
    uint64_t engine_id = 0;
    uint64_t session_number = 0;
    SessionRole role = SessionRole::ORIGINATOR;
    SessionState state = SessionState::ACTIVE;

    /// Originator: the full client service data block.
    /// Receiver: reassembly buffer (resized to total_data_length).
    std::vector<uint8_t> client_data;

    /// Total expected data length for this session.
    uint64_t total_data_length = 0;

    /// True for reliable (red) transfer, false for unreliable (green).
    bool is_red = true;

    /// Receiver only: tracks which byte ranges have been received.
    ReceptionTracker reception_tracker;

    /// Originator: next checkpoint serial number to assign.
    uint64_t checkpoint_serial_counter = 0;

    /// Receiver: next report serial number to assign.
    uint64_t report_serial_counter = 0;

    /// Number of checkpoint retransmissions for this session.
    uint8_t retransmission_count = 0;
};

/// Key for looking up sessions in a map.
struct SessionKey {
    uint64_t engine_id = 0;
    uint64_t session_number = 0;

    bool operator==(const SessionKey& other) const {
        return engine_id == other.engine_id &&
               session_number == other.session_number;
    }
};

/// Hash function for SessionKey.
struct SessionKeyHash {
    size_t operator()(const SessionKey& k) const {
        // Combine hashes using a simple mixing strategy.
        size_t h1 = std::hash<uint64_t>{}(k.engine_id);
        size_t h2 = std::hash<uint64_t>{}(k.session_number);
        return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

}  // namespace ltp
