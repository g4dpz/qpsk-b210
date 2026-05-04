#include "ltp_cla/ltp_engine.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cassert>

namespace ltp {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LtpEngine::LtpEngine(const LtpEngineConfig& config)
    : config_(config) {}

// ---------------------------------------------------------------------------
// start_session — create a new originator session
// ---------------------------------------------------------------------------

uint64_t LtpEngine::start_session(std::vector<uint8_t> data, bool reliable) {
    if (sessions_.size() >= config_.max_concurrent_sessions) {
        spdlog::warn("LTP engine: max concurrent sessions ({}) reached, "
                     "rejecting new session",
                     config_.max_concurrent_sessions);
        return 0;
    }

    uint64_t sn = ++session_counter_;

    SessionKey key{config_.local_engine_id, sn};
    LtpSession session;
    session.engine_id = config_.local_engine_id;
    session.session_number = sn;
    session.role = SessionRole::ORIGINATOR;
    session.state = SessionState::ACTIVE;
    session.total_data_length = data.size();
    session.is_red = reliable;
    session.client_data = std::move(data);

    sessions_.emplace(key, std::move(session));
    diagnostics_.sessions_originated++;

    spdlog::debug("LTP engine: started originator session {} ({}), data size {}",
                  sn, reliable ? "red" : "green",
                  sessions_[key].total_data_length);

    // Segment and send
    auto& sess_ref = sessions_[key];
    auto segments = segment_data(sess_ref);
    for (const auto& seg : segments) {
        send_segment(seg);
    }

    // Green originator: auto-complete after all segments sent (no handshake)
    if (!reliable) {
        sessions_.erase(key);
        diagnostics_.sessions_completed++;
    }

    return sn;
}

// ---------------------------------------------------------------------------
// segment_data_impl — shared implementation
// ---------------------------------------------------------------------------

static std::vector<LtpSegment> segment_data_impl(
    const LtpSession& session, uint32_t max_seg, uint64_t& cp_serial_out) {
    std::vector<LtpSegment> segments;
    const auto& data = session.client_data;

    if (data.empty()) {
        LtpSegment seg;
        seg.engine_id = session.engine_id;
        seg.session_number = session.session_number;

        DataSegContent ds;
        ds.client_service_id = 1;
        ds.offset = 0;
        ds.length = 0;

        if (session.is_red) {
            seg.segment_type = SegType::RED_DATA_CP_EORP_EOB;
            ds.checkpoint_serial = ++cp_serial_out;
            ds.report_serial = 0;
        } else {
            seg.segment_type = SegType::GREEN_DATA_EOB;
        }

        seg.content = std::move(ds);
        segments.push_back(std::move(seg));
        return segments;
    }

    size_t offset = 0;
    size_t remaining = data.size();

    while (remaining > 0) {
        size_t chunk = std::min(static_cast<size_t>(max_seg), remaining);
        bool is_last = (chunk == remaining);

        LtpSegment seg;
        seg.engine_id = session.engine_id;
        seg.session_number = session.session_number;

        DataSegContent ds;
        ds.client_service_id = 1;
        ds.offset = offset;
        ds.length = chunk;
        ds.data.assign(data.begin() + static_cast<ptrdiff_t>(offset),
                       data.begin() + static_cast<ptrdiff_t>(offset + chunk));

        if (session.is_red) {
            if (is_last) {
                seg.segment_type = SegType::RED_DATA_CP_EORP_EOB;
                ds.checkpoint_serial = ++cp_serial_out;
                ds.report_serial = 0;
            } else {
                seg.segment_type = SegType::RED_DATA;
            }
        } else {
            seg.segment_type = is_last ? SegType::GREEN_DATA_EOB
                                       : SegType::GREEN_DATA;
        }

        seg.content = std::move(ds);
        segments.push_back(std::move(seg));

        offset += chunk;
        remaining -= chunk;
    }

    return segments;
}

// ---------------------------------------------------------------------------
// segment_data — mutable: updates session checkpoint counter
// ---------------------------------------------------------------------------

std::vector<LtpSegment> LtpEngine::segment_data(LtpSession& session) const {
    return segment_data_impl(session, config_.max_segment_size,
                             session.checkpoint_serial_counter);
}

// ---------------------------------------------------------------------------
// segment_data_readonly — const: uses a temporary counter
// ---------------------------------------------------------------------------

std::vector<LtpSegment> LtpEngine::segment_data_readonly(const LtpSession& session) const {
    uint64_t tmp_counter = session.checkpoint_serial_counter;
    return segment_data_impl(session, config_.max_segment_size, tmp_counter);
}

// ---------------------------------------------------------------------------
// receive_segment — process an incoming segment
// ---------------------------------------------------------------------------

void LtpEngine::receive_segment(const LtpSegment& segment) {
    if (SegType::is_data(segment.segment_type)) {
        receive_data_segment(segment);
    } else if (segment.segment_type == SegType::REPORT_SEGMENT) {
        receive_report_segment(segment);
    } else if (segment.segment_type == SegType::REPORT_ACK) {
        receive_report_ack(segment);
    } else if (segment.segment_type == SegType::CANCEL_BY_SENDER ||
               segment.segment_type == SegType::CANCEL_BY_RECEIVER) {
        receive_cancel_segment(segment);
    } else if (segment.segment_type == SegType::CANCEL_ACK_TO_SENDER ||
               segment.segment_type == SegType::CANCEL_ACK_TO_RECEIVER) {
        receive_cancel_ack(segment);
    }
}

// ---------------------------------------------------------------------------
// receive_data_segment — handle incoming red/green data
// ---------------------------------------------------------------------------

void LtpEngine::receive_data_segment(const LtpSegment& segment) {
    const auto& ds = std::get<DataSegContent>(segment.content);

    SessionKey key{segment.engine_id, segment.session_number};
    auto it = sessions_.find(key);

    if (it == sessions_.end()) {
        // New receiver session
        if (sessions_.size() >= config_.max_concurrent_sessions) {
            spdlog::warn("LTP engine: max sessions reached, dropping incoming "
                         "data segment for ({}, {})",
                         segment.engine_id, segment.session_number);
            return;
        }

        LtpSession session;
        session.engine_id = segment.engine_id;
        session.session_number = segment.session_number;
        session.role = SessionRole::RECEIVER;
        session.state = SessionState::ACTIVE;
        session.is_red = SegType::is_red(segment.segment_type);

        auto [inserted, _] = sessions_.emplace(key, std::move(session));
        it = inserted;
        diagnostics_.sessions_received++;

        spdlog::debug("LTP engine: created receiver session ({}, {})",
                      segment.engine_id, segment.session_number);
    }

    auto& session = it->second;

    if (session.state != SessionState::ACTIVE) {
        spdlog::debug("LTP engine: ignoring data for non-active session ({}, {})",
                      segment.engine_id, segment.session_number);
        return;
    }

    // Track diagnostics
    if (SegType::is_red(segment.segment_type)) {
        diagnostics_.red_segments_received++;
    } else {
        diagnostics_.green_segments_received++;
    }

    // Store the data in the reassembly buffer
    uint64_t seg_end = ds.offset + ds.length;
    if (seg_end > session.client_data.size()) {
        session.client_data.resize(static_cast<size_t>(seg_end));
    }
    if (ds.length > 0) {
        std::copy(ds.data.begin(), ds.data.end(),
                  session.client_data.begin() + static_cast<ptrdiff_t>(ds.offset));
    }

    // Track received byte range
    session.reception_tracker.add_range(ds.offset, ds.length);

    // Update total data length if we see EOB
    if (SegType::has_eob(segment.segment_type)) {
        session.total_data_length = seg_end;
    }

    // If this is a checkpoint, generate a report segment
    if (SegType::has_checkpoint(segment.segment_type) && ds.checkpoint_serial.has_value()) {
        generate_report(session, ds.checkpoint_serial.value(), seg_end);
    }

    // For green data with EOB, deliver immediately (no handshake)
    if (SegType::is_green(segment.segment_type) && SegType::has_eob(segment.segment_type)) {
        session.state = SessionState::COMPLETED;
        diagnostics_.sessions_completed++;

        spdlog::debug("LTP engine: green receiver session ({}, {}) completed, "
                      "delivering {} bytes",
                      session.engine_id, session.session_number,
                      session.total_data_length);

        if (on_data_arrival_) {
            std::vector<uint8_t> delivered(
                session.client_data.begin(),
                session.client_data.begin() +
                    static_cast<ptrdiff_t>(session.total_data_length));
            on_data_arrival_(std::move(delivered));
        }

        sessions_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// generate_report — build and send a Report_Segment for a checkpoint
// ---------------------------------------------------------------------------

void LtpEngine::generate_report(LtpSession& session,
                                 uint64_t checkpoint_serial,
                                 uint64_t upper_bound) {
    uint64_t report_serial = ++session.report_serial_counter;

    LtpSegment report;
    report.segment_type = SegType::REPORT_SEGMENT;
    report.engine_id = session.engine_id;
    report.session_number = session.session_number;

    ReportSegContent rs;
    rs.report_serial = report_serial;
    rs.checkpoint_serial = checkpoint_serial;
    rs.upper_bound = upper_bound;
    rs.lower_bound = 0;
    rs.claims = session.reception_tracker.get_claims();

    report.content = std::move(rs);

    spdlog::debug("LTP engine: sending report serial {} for checkpoint {} "
                  "session ({}, {}), claims: {}",
                  report_serial, checkpoint_serial,
                  session.engine_id, session.session_number,
                  session.reception_tracker.get_claims().size());

    send_segment(report);
}

// ---------------------------------------------------------------------------
// receive_report_segment — originator handles incoming report
// ---------------------------------------------------------------------------

void LtpEngine::receive_report_segment(const LtpSegment& segment) {
    diagnostics_.reports_received++;

    const auto& rs = std::get<ReportSegContent>(segment.content);

    // Look up the originator session
    SessionKey key{config_.local_engine_id, segment.session_number};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        spdlog::debug("LTP engine: report for unknown session ({}, {})",
                      config_.local_engine_id, segment.session_number);
        // Still send report-ack per RFC 5326
        send_report_ack(segment.engine_id, segment.session_number,
                        rs.report_serial);
        return;
    }

    auto& session = it->second;
    if (session.role != SessionRole::ORIGINATOR) {
        spdlog::debug("LTP engine: report received for non-originator session");
        return;
    }
    if (session.state != SessionState::ACTIVE) {
        return;
    }

    // Send report acknowledgment
    send_report_ack(segment.engine_id, segment.session_number,
                    rs.report_serial);

    // Cancel checkpoint retransmission timer for this checkpoint
    auto cp_key = timer_key(segment.session_number, rs.checkpoint_serial);
    auto cp_it = checkpoint_timers_.find(cp_key);
    if (cp_it != checkpoint_timers_.end()) {
        if (timer_manager_) timer_manager_->cancel_timer(cp_it->second);
        checkpoint_timers_.erase(cp_it);
    }

    // Check if report indicates full coverage
    bool fully_received = false;
    if (session.total_data_length == 0) {
        // Empty payload — any report (even with 0 claims) means complete
        fully_received = true;
    } else if (rs.claims.size() == 1 &&
               rs.claims[0].offset == 0 &&
               rs.claims[0].length >= session.total_data_length) {
        fully_received = true;
    }

    if (fully_received) {
        // Session complete
        session.state = SessionState::COMPLETED;
        diagnostics_.sessions_completed++;

        spdlog::debug("LTP engine: originator session ({}, {}) completed — "
                      "full coverage reported",
                      session.engine_id, session.session_number);

        if (on_session_complete_) {
            on_session_complete_(session.session_number);
        }
        sessions_.erase(it);
        return;
    }

    // Incomplete — selective retransmission needed
    // Find gaps: byte ranges in [rs.lower_bound, rs.upper_bound) not covered
    // by the reception claims
    auto gaps = compute_gaps(rs.claims, rs.lower_bound, rs.upper_bound);

    if (gaps.empty()) {
        // Claims cover the scope but not the full block — this can happen
        // if the report scope doesn't cover the entire block yet.
        // Wait for more reports.
        return;
    }

    // Check retransmission limit
    session.retransmission_count++;
    if (session.retransmission_count > config_.max_retransmissions) {
        spdlog::warn("LTP engine: max retransmissions ({}) reached for "
                     "session ({}, {}), cancelling",
                     config_.max_retransmissions,
                     session.engine_id, session.session_number);
        cancel_session(session.engine_id, session.session_number, 0x01);
        return;
    }

    diagnostics_.retransmissions++;

    spdlog::debug("LTP engine: retransmitting {} gap(s) for session ({}, {}), "
                  "attempt {}",
                  gaps.size(), session.engine_id, session.session_number,
                  session.retransmission_count);

    // Retransmit data for each gap, with the last segment as a new checkpoint
    retransmit_gaps(session, gaps);
}

// ---------------------------------------------------------------------------
// compute_gaps — find unreported byte ranges within [lower, upper)
// ---------------------------------------------------------------------------

std::vector<ReceptionClaim> LtpEngine::compute_gaps(
    const std::vector<ReceptionClaim>& claims,
    uint64_t lower, uint64_t upper) {

    std::vector<ReceptionClaim> gaps;
    uint64_t cursor = lower;

    for (const auto& c : claims) {
        if (cursor >= upper) break;
        uint64_t c_end = c.offset + c.length;
        if (c_end <= cursor) continue;

        if (c.offset > cursor) {
            uint64_t gap_end = std::min(c.offset, upper);
            gaps.push_back({cursor, gap_end - cursor});
        }
        cursor = std::max(cursor, c_end);
    }

    if (cursor < upper) {
        gaps.push_back({cursor, upper - cursor});
    }

    return gaps;
}

// ---------------------------------------------------------------------------
// retransmit_gaps — resend data for gap ranges with new checkpoint
// ---------------------------------------------------------------------------

void LtpEngine::retransmit_gaps(LtpSession& session,
                                 const std::vector<ReceptionClaim>& gaps) {
    uint32_t max_seg = config_.max_segment_size;

    // Collect all segments to send, then mark the last one as checkpoint
    std::vector<LtpSegment> segments;

    for (const auto& gap : gaps) {
        uint64_t offset = gap.offset;
        uint64_t remaining = gap.length;

        while (remaining > 0) {
            uint64_t chunk = std::min(static_cast<uint64_t>(max_seg), remaining);

            LtpSegment seg;
            seg.engine_id = session.engine_id;
            seg.session_number = session.session_number;
            seg.segment_type = SegType::RED_DATA;  // will fix last one below

            DataSegContent ds;
            ds.client_service_id = 1;
            ds.offset = offset;
            ds.length = chunk;
            ds.data.assign(
                session.client_data.begin() + static_cast<ptrdiff_t>(offset),
                session.client_data.begin() + static_cast<ptrdiff_t>(offset + chunk));

            seg.content = std::move(ds);
            segments.push_back(std::move(seg));

            offset += chunk;
            remaining -= chunk;
        }
    }

    // Mark the last retransmitted segment as a checkpoint
    if (!segments.empty()) {
        auto& last = segments.back();
        last.segment_type = SegType::RED_DATA_CP;  // checkpoint, not EORP
        auto& last_ds = std::get<DataSegContent>(last.content);
        last_ds.checkpoint_serial = ++session.checkpoint_serial_counter;
        last_ds.report_serial = 0;
    }

    for (const auto& seg : segments) {
        send_segment(seg);
    }
}

// ---------------------------------------------------------------------------
// send_report_ack — send a Report_Acknowledgment
// ---------------------------------------------------------------------------

void LtpEngine::send_report_ack(uint64_t engine_id, uint64_t session_number,
                                 uint64_t report_serial) {
    LtpSegment ack;
    ack.segment_type = SegType::REPORT_ACK;
    ack.engine_id = engine_id;
    ack.session_number = session_number;
    ack.content = RptAckContent{report_serial};

    send_segment(ack);
}

// ---------------------------------------------------------------------------
// receive_report_ack — receiver handles incoming report-ack
// ---------------------------------------------------------------------------

void LtpEngine::receive_report_ack(const LtpSegment& segment) {
    diagnostics_.report_acks_received++;

    const auto& ra = std::get<RptAckContent>(segment.content);

    // Look up the receiver session
    SessionKey key{segment.engine_id, segment.session_number};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        spdlog::debug("LTP engine: report-ack for unknown session ({}, {})",
                      segment.engine_id, segment.session_number);
        return;
    }

    auto& session = it->second;
    if (session.role != SessionRole::RECEIVER) {
        spdlog::debug("LTP engine: report-ack received for non-receiver session");
        return;
    }

    spdlog::debug("LTP engine: received report-ack serial {} for session ({}, {})",
                  ra.report_serial, segment.engine_id, segment.session_number);

    // Cancel report retransmission timer
    auto rpt_key = timer_key(segment.session_number, ra.report_serial);
    auto rpt_it = report_timers_.find(rpt_key);
    if (rpt_it != report_timers_.end()) {
        if (timer_manager_) timer_manager_->cancel_timer(rpt_it->second);
        report_timers_.erase(rpt_it);
    }

    // Check if the session is complete: all data received and report acknowledged
    if (session.reception_tracker.is_complete(session.total_data_length)) {

        session.state = SessionState::COMPLETED;
        diagnostics_.sessions_completed++;

        spdlog::debug("LTP engine: receiver session ({}, {}) completed — "
                      "all data received and report acknowledged",
                      session.engine_id, session.session_number);

        if (on_data_arrival_) {
            // Deliver the reassembled data, trimmed to total_data_length
            std::vector<uint8_t> delivered(
                session.client_data.begin(),
                session.client_data.begin() +
                    static_cast<ptrdiff_t>(session.total_data_length));
            on_data_arrival_(std::move(delivered));
        }

        sessions_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// handle_timer_expiry — process timer expiry from TimerManager
// ---------------------------------------------------------------------------

void LtpEngine::handle_timer_expiry(const TimerEvent& event) {
    if (event.type == TimerType::CHECKPOINT) {
        // Checkpoint timer expired — retransmit the checkpoint
        SessionKey key{config_.local_engine_id, event.session_number};
        auto it = sessions_.find(key);
        if (it == sessions_.end()) return;

        auto& session = it->second;
        if (session.state != SessionState::ACTIVE) return;
        if (session.role != SessionRole::ORIGINATOR) return;

        session.retransmission_count++;
        if (session.retransmission_count > config_.max_retransmissions) {
            spdlog::warn("LTP engine: checkpoint timer: max retransmissions "
                         "({}) reached for session ({}, {})",
                         config_.max_retransmissions,
                         session.engine_id, session.session_number);
            cancel_session(session.engine_id, session.session_number, 0x01);
            return;
        }

        diagnostics_.retransmissions++;
        spdlog::debug("LTP engine: checkpoint timer expired for session "
                      "({}, {}), retransmission attempt {}",
                      session.engine_id, session.session_number,
                      session.retransmission_count);

        // Retransmit the entire block as a new checkpoint
        // (simplified: in a full implementation we'd retransmit only the
        // checkpoint segment, but retransmitting all gaps is more robust)
        auto segments = segment_data(session);
        for (const auto& seg : segments) {
            send_segment(seg);
        }

    } else if (event.type == TimerType::REPORT_SEGMENT) {
        // Report timer expired — retransmit the report
        SessionKey key{event.engine_id, event.session_number};
        auto it = sessions_.find(key);
        if (it == sessions_.end()) return;

        auto& session = it->second;
        if (session.state != SessionState::ACTIVE) return;
        if (session.role != SessionRole::RECEIVER) return;

        diagnostics_.retransmissions++;
        spdlog::debug("LTP engine: report timer expired for session ({}, {}), "
                      "retransmitting report",
                      session.engine_id, session.session_number);

        // Regenerate and resend the report for the last known checkpoint
        // Use the current reception state
        if (session.total_data_length > 0) {
            generate_report(session, event.serial_number,
                            session.total_data_length);
        }
    }
}

// ---------------------------------------------------------------------------
// cancel_session
// ---------------------------------------------------------------------------

void LtpEngine::cancel_session(uint64_t engine_id, uint64_t session_number,
                                uint8_t reason) {
    SessionKey key{engine_id, session_number};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        spdlog::debug("LTP engine: cancel_session for unknown session "
                      "({}, {})", engine_id, session_number);
        return;
    }

    auto& session = it->second;

    // Send the appropriate cancel segment
    LtpSegment cancel_seg;
    cancel_seg.engine_id = engine_id;
    cancel_seg.session_number = session_number;
    cancel_seg.segment_type = (session.role == SessionRole::ORIGINATOR)
                                  ? SegType::CANCEL_BY_SENDER
                                  : SegType::CANCEL_BY_RECEIVER;
    cancel_seg.content = CancelContent{reason};
    send_segment(cancel_seg);

    session.state = SessionState::CANCELLED;
    diagnostics_.sessions_cancelled++;

    spdlog::debug("LTP engine: cancelled session ({}, {}) reason {}",
                  engine_id, session_number, reason);

    if (on_session_failure_) {
        on_session_failure_(session_number, reason);
    }

    sessions_.erase(it);
}

// ---------------------------------------------------------------------------
// receive_cancel_segment — handle incoming cancel from remote
// ---------------------------------------------------------------------------

void LtpEngine::receive_cancel_segment(const LtpSegment& segment) {
    diagnostics_.cancel_segments_received++;

    const auto& cc = std::get<CancelContent>(segment.content);

    // Send cancel-ack regardless of whether we know the session
    LtpSegment ack;
    ack.engine_id = segment.engine_id;
    ack.session_number = segment.session_number;
    ack.segment_type = (segment.segment_type == SegType::CANCEL_BY_SENDER)
                           ? SegType::CANCEL_ACK_TO_SENDER
                           : SegType::CANCEL_ACK_TO_RECEIVER;
    ack.content = CancelContent{0};
    send_segment(ack);

    // Look up the session
    SessionKey key{segment.engine_id, segment.session_number};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) {
        spdlog::debug("LTP engine: cancel received for unknown session ({}, {})",
                      segment.engine_id, segment.session_number);
        return;
    }

    auto& session = it->second;
    session.state = SessionState::CANCELLED;
    diagnostics_.sessions_cancelled++;

    spdlog::debug("LTP engine: session ({}, {}) cancelled by remote, reason {}",
                  segment.engine_id, segment.session_number, cc.reason_code);

    if (on_session_failure_) {
        on_session_failure_(session.session_number, cc.reason_code);
    }

    sessions_.erase(it);
}

// ---------------------------------------------------------------------------
// receive_cancel_ack — handle incoming cancel acknowledgment
// ---------------------------------------------------------------------------

void LtpEngine::receive_cancel_ack(const LtpSegment& segment) {
    spdlog::debug("LTP engine: received cancel-ack for session ({}, {})",
                  segment.engine_id, segment.session_number);

    // Cancel any pending cancel retransmission timer (if we had one)
    // The session should already be cleaned up by cancel_session,
    // but clean up if it's still around.
    SessionKey key{segment.engine_id, segment.session_number};
    auto it = sessions_.find(key);
    if (it != sessions_.end() && it->second.state == SessionState::CANCELLED) {
        sessions_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// send_segment — encode and send via callback
// ---------------------------------------------------------------------------

void LtpEngine::send_segment(const LtpSegment& seg) {
    if (SegType::is_red(seg.segment_type)) {
        diagnostics_.red_segments_sent++;
        if (SegType::has_checkpoint(seg.segment_type)) {
            diagnostics_.checkpoints_sent++;
            // Start checkpoint retransmission timer
            if (timer_manager_) {
                auto& ds = std::get<DataSegContent>(seg.content);
                if (ds.checkpoint_serial.has_value()) {
                    TimerEvent evt;
                    evt.type = TimerType::CHECKPOINT;
                    evt.engine_id = seg.engine_id;
                    evt.session_number = seg.session_number;
                    evt.serial_number = ds.checkpoint_serial.value();
                    auto tid = timer_manager_->start_timer(
                        std::chrono::milliseconds(config_.retransmission_timeout_ms),
                        evt);
                    checkpoint_timers_[timer_key(seg.session_number,
                                                 ds.checkpoint_serial.value())] = tid;
                }
            }
        }
    } else if (SegType::is_green(seg.segment_type)) {
        diagnostics_.green_segments_sent++;
    } else if (seg.segment_type == SegType::REPORT_SEGMENT) {
        diagnostics_.reports_sent++;
        // Start report retransmission timer
        if (timer_manager_) {
            auto& rs = std::get<ReportSegContent>(seg.content);
            TimerEvent evt;
            evt.type = TimerType::REPORT_SEGMENT;
            evt.engine_id = seg.engine_id;
            evt.session_number = seg.session_number;
            evt.serial_number = rs.report_serial;
            auto tid = timer_manager_->start_timer(
                std::chrono::milliseconds(config_.retransmission_timeout_ms),
                evt);
            report_timers_[timer_key(seg.session_number,
                                     rs.report_serial)] = tid;
        }
    } else if (seg.segment_type == SegType::REPORT_ACK) {
        diagnostics_.report_acks_sent++;
    } else if (seg.segment_type == SegType::CANCEL_BY_SENDER ||
               seg.segment_type == SegType::CANCEL_BY_RECEIVER) {
        diagnostics_.cancel_segments_sent++;
    }

    if (on_send_segment_) {
        on_send_segment_(seg.encode());
    }
}

// ---------------------------------------------------------------------------
// get_session — lookup for testing
// ---------------------------------------------------------------------------

const LtpSession* LtpEngine::get_session(uint64_t engine_id,
                                           uint64_t session_number) const {
    SessionKey key{engine_id, session_number};
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return nullptr;
    return &it->second;
}

}  // namespace ltp
