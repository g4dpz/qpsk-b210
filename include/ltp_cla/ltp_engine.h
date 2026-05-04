#pragma once

#include "ltp_cla/ltp_segment.h"
#include "ltp_cla/ltp_session.h"
#include "ltp_cla/timer_manager.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ltp {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct LtpEngineConfig {
    uint64_t local_engine_id = 1;
    uint64_t remote_engine_id = 2;
    uint32_t max_segment_size = 1400;            // bytes
    uint32_t retransmission_timeout_ms = 30000;  // 30 seconds
    uint8_t  max_retransmissions = 10;
    uint32_t max_concurrent_sessions = 100;
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

struct LtpDiagnostics {
    uint64_t sessions_originated = 0;
    uint64_t sessions_received = 0;
    uint64_t sessions_completed = 0;
    uint64_t sessions_cancelled = 0;
    uint64_t red_segments_sent = 0;
    uint64_t red_segments_received = 0;
    uint64_t green_segments_sent = 0;
    uint64_t green_segments_received = 0;
    uint64_t checkpoints_sent = 0;
    uint64_t reports_received = 0;
    uint64_t reports_sent = 0;
    uint64_t report_acks_sent = 0;
    uint64_t report_acks_received = 0;
    uint64_t retransmissions = 0;
    uint64_t cancel_segments_sent = 0;
    uint64_t cancel_segments_received = 0;
};

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

/// Called when reassembled data is ready for delivery (receiver session complete).
using DataArrivalCallback = std::function<void(std::vector<uint8_t> data)>;

/// Called when an originator session completes successfully.
using SessionCompleteCallback = std::function<void(uint64_t session_number)>;

/// Called when a session fails (cancelled or max retransmissions).
using SessionFailureCallback = std::function<void(uint64_t session_number, uint8_t reason)>;

/// Called when the engine needs to send an encoded segment over the CLA.
using SendSegmentCallback = std::function<void(std::vector<uint8_t> encoded_segment)>;

// ---------------------------------------------------------------------------
// LTP Engine
// ---------------------------------------------------------------------------

class LtpEngine {
public:
    explicit LtpEngine(const LtpEngineConfig& config);

    /// Start a new originator session to transmit the given data.
    /// @param data     Client service data (opaque byte blob).
    /// @param reliable True for red (reliable) transfer, false for green.
    /// @return Session number, or 0 if max concurrent sessions reached.
    uint64_t start_session(std::vector<uint8_t> data, bool reliable);

    /// Process a received (decoded) LTP segment.
    void receive_segment(const LtpSegment& segment);

    /// Cancel a session by session key.
    void cancel_session(uint64_t engine_id, uint64_t session_number, uint8_t reason);

    /// Handle a timer expiry event from the TimerManager.
    void handle_timer_expiry(const TimerEvent& event);

    /// Set the timer manager for retransmission timers. Optional — if not set,
    /// no automatic retransmission occurs (useful for unit testing).
    void set_timer_manager(TimerManager* tm) { timer_manager_ = tm; }

    /// Segment the client data for a session into LTP segments.
    /// Returns the list of segments ready to send.
    /// Updates the session's checkpoint_serial_counter for red data.
    std::vector<LtpSegment> segment_data(LtpSession& session) const;

    /// Const overload for testing — does not update session counters.
    std::vector<LtpSegment> segment_data_readonly(const LtpSession& session) const;

    // ---- Callbacks ----

    void set_data_arrival_callback(DataArrivalCallback cb) { on_data_arrival_ = std::move(cb); }
    void set_session_complete_callback(SessionCompleteCallback cb) { on_session_complete_ = std::move(cb); }
    void set_session_failure_callback(SessionFailureCallback cb) { on_session_failure_ = std::move(cb); }
    void set_send_segment_callback(SendSegmentCallback cb) { on_send_segment_ = std::move(cb); }

    // ---- Accessors ----

    const LtpEngineConfig& config() const { return config_; }
    LtpDiagnostics get_diagnostics() const { return diagnostics_; }
    size_t active_session_count() const { return sessions_.size(); }

    /// Get a session by key (for testing). Returns nullptr if not found.
    const LtpSession* get_session(uint64_t engine_id, uint64_t session_number) const;

private:
    void send_segment(const LtpSegment& seg);
    void receive_data_segment(const LtpSegment& segment);
    void generate_report(LtpSession& session, uint64_t checkpoint_serial,
                         uint64_t upper_bound);
    void receive_report_segment(const LtpSegment& segment);
    void receive_report_ack(const LtpSegment& segment);
    void send_report_ack(uint64_t engine_id, uint64_t session_number,
                         uint64_t report_serial);
    void retransmit_gaps(LtpSession& session,
                         const std::vector<ReceptionClaim>& gaps);
    void receive_cancel_segment(const LtpSegment& segment);
    void receive_cancel_ack(const LtpSegment& segment);
    static std::vector<ReceptionClaim> compute_gaps(
        const std::vector<ReceptionClaim>& claims,
        uint64_t lower, uint64_t upper);

    LtpEngineConfig config_;
    uint64_t session_counter_ = 0;
    std::unordered_map<SessionKey, LtpSession, SessionKeyHash> sessions_;
    LtpDiagnostics diagnostics_;
    TimerManager* timer_manager_ = nullptr;

    // Maps checkpoint/report serial → timer ID for cancellation
    // Key: (session_number << 32) | serial_number
    std::unordered_map<uint64_t, uint64_t> checkpoint_timers_;
    std::unordered_map<uint64_t, uint64_t> report_timers_;

    static uint64_t timer_key(uint64_t session_number, uint64_t serial) {
        return (session_number << 32) | (serial & 0xFFFFFFFF);
    }

    DataArrivalCallback on_data_arrival_;
    SessionCompleteCallback on_session_complete_;
    SessionFailureCallback on_session_failure_;
    SendSegmentCallback on_send_segment_;
};

}  // namespace ltp
