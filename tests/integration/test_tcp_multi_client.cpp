// Integration tests: TCP Input (single-client) and Output (multi-client) servers
//
// Input server: single client connects, sends data, disconnects, reconnects
// Output server: multiple clients connect, receive broadcast, handle disconnects
//
// Validates: Requirements 11.1–11.8, 12.1–12.8

#include <gtest/gtest.h>

#include "qpsk_b200/tcp_input_server.h"
#include "qpsk_b200/tcp_output_server.h"

#include <chrono>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

uint16_t random_port() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint16_t> dist(49152, 60000);
    return dist(rng);
}

int connect_client(uint16_t port) {
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

bool send_all(int fd, const std::vector<uint8_t>& data) {
    const uint8_t* ptr = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (n <= 0) return false;
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

std::vector<uint8_t> recv_all(int fd, size_t max_bytes, int timeout_ms = 300) {
    std::vector<uint8_t> result;
    result.reserve(max_bytes);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (result.size() < max_bytes) {
        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;
        int ret = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ret <= 0) break;
        uint8_t buf[4096];
        ssize_t n = ::recv(fd, buf, std::min(sizeof(buf), max_bytes - result.size()), 0);
        if (n <= 0) break;
        result.insert(result.end(), buf, buf + n);
    }
    return result;
}

} // anonymous namespace

// ===========================================================================
// TCP Input Server — Single Client Integration Tests
// ===========================================================================

class TcpInputIntegrationTest : public ::testing::Test {
protected:
    uint16_t port_ = random_port();
};

TEST_F(TcpInputIntegrationTest, ClientSendsDataAndDisconnects) {
    const size_t frame_size = 8;
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, frame_size);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(20);
    EXPECT_TRUE(server.connected());

    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    ASSERT_TRUE(send_all(c1, data));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);

    EXPECT_EQ(server.buffered_bytes(), 8u);
    auto frame = server.read_frame();
    EXPECT_EQ(frame, data);

    ::close(c1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);
    EXPECT_FALSE(server.connected());

    server.stop();
}

TEST_F(TcpInputIntegrationTest, DisconnectAndReconnect) {
    const size_t frame_size = 8;
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, frame_size);
    server.start();

    // First client sends partial data
    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(20);

    std::vector<uint8_t> d1 = {0x01, 0x02, 0x03, 0x04};
    ASSERT_TRUE(send_all(c1, d1));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);

    ::close(c1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);
    EXPECT_FALSE(server.connected());
    EXPECT_EQ(server.buffered_bytes(), 4u);  // data preserved

    // Second client connects and sends remaining data
    int c2 = connect_client(port_);
    ASSERT_GE(c2, 0);
    server.poll_once(20);
    EXPECT_TRUE(server.connected());

    std::vector<uint8_t> d2 = {0x05, 0x06, 0x07, 0x08};
    ASSERT_TRUE(send_all(c2, d2));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);

    EXPECT_EQ(server.buffered_bytes(), 8u);
    auto frame = server.read_frame();
    std::vector<uint8_t> expected = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    EXPECT_EQ(frame, expected);

    ::close(c2);
    server.stop();
}

TEST_F(TcpInputIntegrationTest, ByteOrderPreserved) {
    const size_t frame_size = 16;
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, frame_size);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(20);

    // Send in two batches
    std::vector<uint8_t> batch1(8);
    std::iota(batch1.begin(), batch1.end(), 1);
    std::vector<uint8_t> batch2(8);
    std::iota(batch2.begin(), batch2.end(), 9);

    ASSERT_TRUE(send_all(c1, batch1));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);

    ASSERT_TRUE(send_all(c1, batch2));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    server.poll_once(20);

    auto frame = server.read_frame();
    std::vector<uint8_t> expected(16);
    std::iota(expected.begin(), expected.end(), 1);
    EXPECT_EQ(frame, expected);

    ::close(c1);
    server.stop();
}

// ===========================================================================
// TCP Output Server — Multi-Client Integration Tests
// ===========================================================================

class TcpOutputIntegrationTest : public ::testing::Test {
protected:
    uint16_t port_ = random_port();
};

TEST_F(TcpOutputIntegrationTest, ThreeClientsReceiveBroadcast) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(20);
    int c2 = connect_client(port_);
    ASSERT_GE(c2, 0);
    server.poll_once(20);
    int c3 = connect_client(port_);
    ASSERT_GE(c3, 0);
    server.poll_once(20);

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    server.write_to_all(payload);

    EXPECT_EQ(recv_all(c1, payload.size()), payload);
    EXPECT_EQ(recv_all(c2, payload.size()), payload);
    EXPECT_EQ(recv_all(c3, payload.size()), payload);

    ::close(c1);
    ::close(c2);
    ::close(c3);
    server.stop();
}

TEST_F(TcpOutputIntegrationTest, DisconnectDoesNotAffectOthers) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(20);
    int c2 = connect_client(port_);
    ASSERT_GE(c2, 0);
    server.poll_once(20);

    ::close(c1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    server.write_to_all(payload);

    EXPECT_EQ(recv_all(c2, payload.size()), payload);

    ::close(c2);
    server.stop();
}

TEST_F(TcpOutputIntegrationTest, BroadcastPreservesOrder) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(20);

    server.write_to_all({0x10, 0x20});
    server.write_to_all({0x30, 0x40});
    server.write_to_all({0x50, 0x60});

    auto received = recv_all(c1, 6);
    EXPECT_EQ(received, (std::vector<uint8_t>{0x10, 0x20, 0x30, 0x40, 0x50, 0x60}));

    ::close(c1);
    server.stop();
}
