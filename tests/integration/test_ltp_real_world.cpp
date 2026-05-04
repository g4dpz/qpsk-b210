/// Real-world integration tests for the LTP CLA.
///
/// These tests simulate operational scenarios that would occur on a
/// ground station ↔ satellite link using ION-DTN + QPSK codec.

#include "ltp_cla/convergence_layer_adapter.h"
#include "ltp_cla/ltp_engine.h"
#include "ltp_cla/tcp_egress.h"
#include "ltp_cla/tcp_ingress.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int connect_local(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

static bool write_all(int fd, const std::vector<uint8_t>& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n <= 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

static std::vector<uint8_t> read_with_timeout(int fd, size_t expected,
                                               int timeout_ms = 5000) {
    std::vector<uint8_t> result;
    uint8_t buf[8192];
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (result.size() < expected &&
           std::chrono::steady_clock::now() < deadline) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, 50);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) break;
            result.insert(result.end(), buf, buf + n);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Fixture: bidirectional link — two full stacks wired together
//
//   Node A (ground station):
//     ingress_a → engine_a → [wire] → engine_b → egress_b
//
//   Node B (satellite):
//     ingress_b → engine_b → [wire] → engine_a → egress_a
// ---------------------------------------------------------------------------

class BidirectionalLinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        LtpEngineConfig cfg_a;
        cfg_a.local_engine_id = 1;
        cfg_a.remote_engine_id = 2;
        cfg_a.max_segment_size = 200;
        engine_a_ = std::make_unique<LtpEngine>(cfg_a);

        LtpEngineConfig cfg_b;
        cfg_b.local_engine_id = 2;
        cfg_b.remote_engine_id = 1;
        cfg_b.max_segment_size = 200;
        engine_b_ = std::make_unique<LtpEngine>(cfg_b);

        // Wire: A→B and B→A
        engine_a_->set_send_segment_callback([this](std::vector<uint8_t> enc) {
            auto seg = LtpSegment::decode(enc.data(), enc.size());
            if (seg) engine_b_->receive_segment(*seg);
        });
        engine_b_->set_send_segment_callback([this](std::vector<uint8_t> enc) {
            auto seg = LtpSegment::decode(enc.data(), enc.size());
            if (seg) engine_a_->receive_segment(*seg);
        });

        // Egress A (receives data from B→A path)
        EgressConfig ecfg_a;
        ecfg_a.port = 0;
        egress_a_ = std::make_unique<TcpEgress>(ecfg_a);
        engine_a_->set_data_arrival_callback([this](std::vector<uint8_t> data) {
            egress_a_->deliver(std::move(data));
        });

        // Egress B (receives data from A→B path)
        EgressConfig ecfg_b;
        ecfg_b.port = 0;
        egress_b_ = std::make_unique<TcpEgress>(ecfg_b);
        engine_b_->set_data_arrival_callback([this](std::vector<uint8_t> data) {
            egress_b_->deliver(std::move(data));
        });

        // Ingress A (sends data into A→B path)
        IngressConfig icfg_a;
        icfg_a.port = 0;
        ingress_a_ = std::make_unique<TcpIngress>(icfg_a);
        ingress_a_->set_data_callback([this](std::vector<uint8_t> data) {
            engine_a_->start_session(std::move(data), true);
        });

        // Ingress B (sends data into B→A path)
        IngressConfig icfg_b;
        icfg_b.port = 0;
        ingress_b_ = std::make_unique<TcpIngress>(icfg_b);
        ingress_b_->set_data_callback([this](std::vector<uint8_t> data) {
            engine_b_->start_session(std::move(data), true);
        });

        egress_a_->start();
        egress_b_->start();
        ingress_a_->start();
        ingress_b_->start();
    }

    void TearDown() override {
        ingress_a_->stop();
        ingress_b_->stop();
        egress_a_->stop();
        egress_b_->stop();
    }

    std::unique_ptr<LtpEngine> engine_a_, engine_b_;
    std::unique_ptr<TcpIngress> ingress_a_, ingress_b_;
    std::unique_ptr<TcpEgress> egress_a_, egress_b_;
};

// ---------------------------------------------------------------------------
// Bidirectional simultaneous transfer
// ---------------------------------------------------------------------------

TEST_F(BidirectionalLinkTest, SimultaneousTransfer) {
    // Connect egress clients
    int egress_b_client = connect_local(egress_b_->listening_port());
    int egress_a_client = connect_local(egress_a_->listening_port());
    ASSERT_GE(egress_b_client, 0);
    ASSERT_GE(egress_a_client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect ingress clients
    int ingress_a_client = connect_local(ingress_a_->listening_port());
    int ingress_b_client = connect_local(ingress_b_->listening_port());
    ASSERT_GE(ingress_a_client, 0);
    ASSERT_GE(ingress_b_client, 0);

    // A→B: 400 bytes
    std::vector<uint8_t> payload_ab(400);
    std::iota(payload_ab.begin(), payload_ab.end(), static_cast<uint8_t>(0xA0));

    // B→A: 300 bytes
    std::vector<uint8_t> payload_ba(300);
    std::iota(payload_ba.begin(), payload_ba.end(), static_cast<uint8_t>(0xB0));

    // Send both simultaneously
    write_all(ingress_a_client, frame_message(payload_ab));
    write_all(ingress_b_client, frame_message(payload_ba));

    // Read from both egress sides
    auto recv_b = read_with_timeout(egress_b_client, 4 + payload_ab.size());
    auto recv_a = read_with_timeout(egress_a_client, 4 + payload_ba.size());

    std::vector<uint8_t> extracted_b, extracted_a;
    ASSERT_TRUE(extract_framed_message(recv_b, extracted_b));
    ASSERT_TRUE(extract_framed_message(recv_a, extracted_a));

    EXPECT_EQ(extracted_b, payload_ab);
    EXPECT_EQ(extracted_a, payload_ba);

    ::close(ingress_a_client);
    ::close(ingress_b_client);
    ::close(egress_a_client);
    ::close(egress_b_client);
}

// ===========================================================================
// Standalone tests (single-direction, no fixture needed)
// ===========================================================================

// ---------------------------------------------------------------------------
// Green data end-to-end through TCP ingress → engine → engine → TCP egress
// ---------------------------------------------------------------------------

TEST(LtpRealWorldTest, GreenDataEndToEnd) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 200;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 200;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    EgressConfig ecfg;
    ecfg.port = 0;
    TcpEgress egress(ecfg);
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        egress.deliver(std::move(data));
    });

    IngressConfig icfg;
    icfg.port = 0;
    icfg.default_reliable = false;  // green
    TcpIngress ingress(icfg);
    ingress.set_data_callback([&](std::vector<uint8_t> data) {
        engine_a.start_session(std::move(data), false);  // green
    });

    egress.start();
    ingress.start();

    int egress_client = connect_local(egress.listening_port());
    ASSERT_GE(egress_client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int ingress_client = connect_local(ingress.listening_port());
    ASSERT_GE(ingress_client, 0);

    std::vector<uint8_t> payload(500, 0x77);
    write_all(ingress_client, frame_message(payload));

    auto received = read_with_timeout(egress_client, 4 + payload.size());
    std::vector<uint8_t> extracted;
    ASSERT_TRUE(extract_framed_message(received, extracted));
    EXPECT_EQ(extracted, payload);

    // Verify no reports were sent (green = no handshake)
    auto diag_b = engine_b.get_diagnostics();
    EXPECT_EQ(diag_b.reports_sent, 0u);

    ::close(ingress_client);
    ::close(egress_client);
    ingress.stop();
    egress.stop();
}

// ---------------------------------------------------------------------------
// Session cancellation propagation
// ---------------------------------------------------------------------------

TEST(LtpRealWorldTest, SessionCancellationPropagation) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 50;  // small segments to create many
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 50;
    LtpEngine engine_b(cfg_b);

    // Drop all data segments to prevent completion, but pass control segments
    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;
        // Only pass the first data segment (to create the receiver session)
        // and all non-data segments (cancel, cancel-ack)
        static int data_count = 0;
        if (SegType::is_data(seg->segment_type)) {
            data_count++;
            if (data_count > 1) return;  // drop subsequent data
        }
        engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    bool b_failure = false;
    uint8_t b_reason = 0;
    engine_b.set_session_failure_callback([&](uint64_t, uint8_t reason) {
        b_failure = true;
        b_reason = reason;
    });

    // Start a session
    std::vector<uint8_t> payload(200, 0xCC);
    uint64_t sn = engine_a.start_session(payload, true);
    ASSERT_NE(sn, 0u);

    // Originator cancels the session
    engine_a.cancel_session(cfg_a.local_engine_id, sn, 0x05);

    // Receiver should have been notified
    EXPECT_TRUE(b_failure);
    EXPECT_EQ(b_reason, 0x05);
    EXPECT_EQ(engine_a.active_session_count(), 0u);
    EXPECT_EQ(engine_b.active_session_count(), 0u);

    auto diag_a = engine_a.get_diagnostics();
    EXPECT_GE(diag_a.cancel_segments_sent, 1u);
}

// ---------------------------------------------------------------------------
// Back-to-back rapid transfers (stress test)
// ---------------------------------------------------------------------------

TEST(LtpRealWorldTest, RapidFireTransfers) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 200;
    cfg_a.max_concurrent_sessions = 200;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 200;
    cfg_b.max_concurrent_sessions = 200;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    std::mutex mtx;
    std::vector<std::vector<uint8_t>> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        std::lock_guard<std::mutex> lock(mtx);
        delivered.push_back(std::move(data));
    });

    constexpr int NUM_BLOCKS = 50;

    // Send 50 blocks rapidly
    std::vector<std::vector<uint8_t>> sent;
    for (int i = 0; i < NUM_BLOCKS; ++i) {
        std::vector<uint8_t> payload(100 + (i % 50), static_cast<uint8_t>(i));
        sent.push_back(payload);
        uint64_t sn = engine_a.start_session(payload, true);
        ASSERT_NE(sn, 0u) << "Failed to start session " << i;
    }

    // All should have been delivered (no loss on the wire)
    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_EQ(delivered.size(), static_cast<size_t>(NUM_BLOCKS));

    // Verify each delivered block matches what was sent
    // (order may differ since sessions complete independently)
    for (const auto& s : sent) {
        bool found = false;
        for (const auto& d : delivered) {
            if (d == s) { found = true; break; }
        }
        EXPECT_TRUE(found) << "Missing block of size " << s.size()
                           << " starting with " << (int)s[0];
    }

    // Verify session counters
    auto diag = engine_a.get_diagnostics();
    EXPECT_EQ(diag.sessions_originated, static_cast<uint64_t>(NUM_BLOCKS));
    EXPECT_EQ(diag.sessions_completed, static_cast<uint64_t>(NUM_BLOCKS));
    EXPECT_EQ(engine_a.active_session_count(), 0u);
    EXPECT_EQ(engine_b.active_session_count(), 0u);
}

// ---------------------------------------------------------------------------
// Large block near max size (64KB)
// ---------------------------------------------------------------------------

TEST(LtpRealWorldTest, LargeBlockNearMaxSize) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 1400;  // realistic segment size
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 1400;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    // 60KB block → 43 segments at 1400 bytes each
    std::vector<uint8_t> payload(60000);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i * 31 + 17) & 0xFF);
    }

    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);

    auto diag = engine_a.get_diagnostics();
    // 60000 / 1400 = 42.8 → 43 segments
    EXPECT_GE(diag.red_segments_sent, 43u);
}

// ---------------------------------------------------------------------------
// Empty payload transfer
// ---------------------------------------------------------------------------

TEST(LtpRealWorldTest, EmptyPayloadTransfer) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    bool delivered = false;
    size_t delivered_size = 999;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = true;
        delivered_size = data.size();
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    std::vector<uint8_t> payload;  // empty
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_TRUE(delivered);
    EXPECT_EQ(delivered_size, 0u);
    EXPECT_EQ(engine_a.active_session_count(), 0u);
    EXPECT_EQ(engine_b.active_session_count(), 0u);
}
