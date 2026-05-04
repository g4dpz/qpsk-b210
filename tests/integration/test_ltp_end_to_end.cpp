/// Integration test: End-to-end data transfer through two LTP CLA stacks.
///
/// Simulates the full path:
///   Client → TCP Ingress → LTP Engine A → [wire] → LTP Engine B → TCP Egress → Client
///
/// The "wire" is a direct callback (no actual CLA/QPSK codec), simulating
/// a perfect link with no loss.

#include "ltp_cla/convergence_layer_adapter.h"
#include "ltp_cla/ltp_engine.h"
#include "ltp_cla/tcp_egress.h"
#include "ltp_cla/tcp_ingress.h"
#include "ltp_cla/timer_manager.h"

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
                                               int timeout_ms = 3000) {
    std::vector<uint8_t> result;
    uint8_t buf[4096];
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
// Test fixture: two LTP stacks wired together
// ---------------------------------------------------------------------------

class LtpEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Engine A (ground station side)
        LtpEngineConfig cfg_a;
        cfg_a.local_engine_id = 1;
        cfg_a.remote_engine_id = 2;
        cfg_a.max_segment_size = 200;
        engine_a_ = std::make_unique<LtpEngine>(cfg_a);

        // Engine B (satellite side)
        LtpEngineConfig cfg_b;
        cfg_b.local_engine_id = 2;
        cfg_b.remote_engine_id = 1;
        cfg_b.max_segment_size = 200;
        engine_b_ = std::make_unique<LtpEngine>(cfg_b);

        // Wire engines: A sends → B receives, B sends → A receives
        engine_a_->set_send_segment_callback([this](std::vector<uint8_t> enc) {
            auto seg = LtpSegment::decode(enc.data(), enc.size());
            if (seg) engine_b_->receive_segment(*seg);
        });
        engine_b_->set_send_segment_callback([this](std::vector<uint8_t> enc) {
            auto seg = LtpSegment::decode(enc.data(), enc.size());
            if (seg) engine_a_->receive_segment(*seg);
        });

        // Egress on B side (delivers to downstream)
        EgressConfig egress_cfg;
        egress_cfg.port = 0;
        egress_ = std::make_unique<TcpEgress>(egress_cfg);

        engine_b_->set_data_arrival_callback([this](std::vector<uint8_t> data) {
            egress_->deliver(std::move(data));
        });

        // Ingress on A side (accepts from upstream)
        IngressConfig ingress_cfg;
        ingress_cfg.port = 0;
        ingress_ = std::make_unique<TcpIngress>(ingress_cfg);

        ingress_->set_data_callback([this](std::vector<uint8_t> data) {
            engine_a_->start_session(std::move(data), true);
        });

        egress_->start();
        ingress_->start();
    }

    void TearDown() override {
        ingress_->stop();
        egress_->stop();
    }

    std::unique_ptr<LtpEngine> engine_a_;
    std::unique_ptr<LtpEngine> engine_b_;
    std::unique_ptr<TcpIngress> ingress_;
    std::unique_ptr<TcpEgress> egress_;
};

// ---------------------------------------------------------------------------
// Test: single block transfer through the full stack
// ---------------------------------------------------------------------------

TEST_F(LtpEndToEndTest, SingleBlockTransfer) {
    // Connect a client to the egress (receiver side)
    int egress_client = connect_local(egress_->listening_port());
    ASSERT_GE(egress_client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect a client to the ingress (sender side) and send a framed block
    int ingress_client = connect_local(ingress_->listening_port());
    ASSERT_GE(ingress_client, 0);

    std::vector<uint8_t> payload(500);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(0));
    auto framed = frame_message(payload);
    ASSERT_TRUE(write_all(ingress_client, framed));

    // Wait for the data to flow through:
    // ingress → engine A → [wire] → engine B → egress → egress_client
    auto received = read_with_timeout(egress_client, 4 + payload.size());

    std::vector<uint8_t> extracted;
    ASSERT_TRUE(extract_framed_message(received, extracted));
    EXPECT_EQ(extracted, payload);

    ::close(ingress_client);
    ::close(egress_client);
}

// ---------------------------------------------------------------------------
// Test: multiple blocks in sequence
// ---------------------------------------------------------------------------

TEST_F(LtpEndToEndTest, MultipleBlocksSequential) {
    int egress_client = connect_local(egress_->listening_port());
    ASSERT_GE(egress_client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int ingress_client = connect_local(ingress_->listening_port());
    ASSERT_GE(ingress_client, 0);

    // Send 3 blocks
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> payload(100, static_cast<uint8_t>(i + 1));
        auto framed = frame_message(payload);
        ASSERT_TRUE(write_all(ingress_client, framed));
        // Small delay between blocks
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Read all 3 delivered blocks
    // Each: 4 bytes length + 100 bytes data = 104 bytes × 3 = 312 bytes
    auto received = read_with_timeout(egress_client, 312, 5000);

    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> extracted;
        ASSERT_TRUE(extract_framed_message(received, extracted))
            << "Failed to extract block " << i;
        ASSERT_EQ(extracted.size(), 100u);
        EXPECT_EQ(extracted[0], static_cast<uint8_t>(i + 1));
    }

    ::close(ingress_client);
    ::close(egress_client);
}

// ---------------------------------------------------------------------------
// Test: large block (requires multi-segment LTP)
// ---------------------------------------------------------------------------

TEST_F(LtpEndToEndTest, LargeBlockMultiSegment) {
    int egress_client = connect_local(egress_->listening_port());
    ASSERT_GE(egress_client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int ingress_client = connect_local(ingress_->listening_port());
    ASSERT_GE(ingress_client, 0);

    // 1500 bytes with max_segment_size=200 → 8 segments
    std::vector<uint8_t> payload(1500);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(0));
    auto framed = frame_message(payload);
    ASSERT_TRUE(write_all(ingress_client, framed));

    auto received = read_with_timeout(egress_client, 4 + payload.size(), 5000);

    std::vector<uint8_t> extracted;
    ASSERT_TRUE(extract_framed_message(received, extracted));
    EXPECT_EQ(extracted, payload);

    ::close(ingress_client);
    ::close(egress_client);
}
