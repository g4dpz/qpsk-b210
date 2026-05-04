#include <gtest/gtest.h>

#include "qpsk_b200/tcp_input_server.h"
#include "qpsk_b200/tcp_output_server.h"

#include <chrono>
#include <cstring>
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
        ssize_t n = ::send(fd, ptr, remaining, 0);
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
// TcpInputServer tests (single-client)
// ===========================================================================

class TcpInputServerTest : public ::testing::Test {
protected:
    uint16_t port_ = random_port();
};

TEST_F(TcpInputServerTest, StartStop) {
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, 64);
    ASSERT_NO_THROW(server.start());
    EXPECT_FALSE(server.connected());
    EXPECT_EQ(server.buffered_bytes(), 0u);
    EXPECT_FALSE(server.has_frame());
    server.stop();
}

TEST_F(TcpInputServerTest, AcceptClient) {
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, 64);
    server.start();

    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);
    server.poll_once(50);
    EXPECT_TRUE(server.connected());

    ::close(client_fd);
    server.stop();
}

TEST_F(TcpInputServerTest, ReadDataAndSegmentation) {
    const size_t frame_size = 16;
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, frame_size);
    server.start();

    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);
    server.poll_once(50);

    std::vector<uint8_t> data(40);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
    ASSERT_TRUE(send_all(client_fd, data));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.poll_once(50);

    EXPECT_EQ(server.buffered_bytes(), 40u);
    EXPECT_TRUE(server.has_frame());

    auto frame1 = server.read_frame();
    ASSERT_EQ(frame1.size(), frame_size);
    for (size_t i = 0; i < frame_size; ++i) EXPECT_EQ(frame1[i], static_cast<uint8_t>(i));

    auto frame2 = server.read_frame();
    ASSERT_EQ(frame2.size(), frame_size);
    for (size_t i = 0; i < frame_size; ++i) EXPECT_EQ(frame2[i], static_cast<uint8_t>(i + frame_size));

    EXPECT_FALSE(server.has_frame());
    EXPECT_EQ(server.buffered_bytes(), 8u);

    ::close(client_fd);
    server.stop();
}

TEST_F(TcpInputServerTest, ClientDisconnectAndReconnect) {
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, 64);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(50);
    EXPECT_TRUE(server.connected());

    ::close(c1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.poll_once(50);
    EXPECT_FALSE(server.connected());

    // Reconnect
    int c2 = connect_client(port_);
    ASSERT_GE(c2, 0);
    server.poll_once(50);
    EXPECT_TRUE(server.connected());

    ::close(c2);
    server.stop();
}

TEST_F(TcpInputServerTest, NewClientRejectedWhileConnected) {
    qpsk_b200::TcpInputServer server("127.0.0.1", port_, 64);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(50);
    EXPECT_TRUE(server.connected());

    // Second client tries to connect — should be rejected
    int c2 = connect_client(port_);
    ASSERT_GE(c2, 0);  // connect succeeds at TCP level (accept then close)
    server.poll_once(50);
    EXPECT_TRUE(server.connected());

    // c1 should still work — send data and verify
    std::vector<uint8_t> data = {0xAA, 0xBB};
    ASSERT_TRUE(send_all(c1, data));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.poll_once(50);
    EXPECT_EQ(server.buffered_bytes(), 2u);

    ::close(c1);
    ::close(c2);
    server.stop();
}

// ===========================================================================
// TcpOutputServer tests (multi-client, since RX side fans out)
// ===========================================================================

class TcpOutputServerTest : public ::testing::Test {
protected:
    uint16_t port_ = random_port();
};

TEST_F(TcpOutputServerTest, StartStop) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    ASSERT_NO_THROW(server.start());
    EXPECT_EQ(server.client_count(), 0u);
    server.stop();
}

TEST_F(TcpOutputServerTest, AcceptClient) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    server.start();
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);
    server.poll_once(50);
    EXPECT_EQ(server.client_count(), 1u);
    ::close(client_fd);
    server.stop();
}

TEST_F(TcpOutputServerTest, WriteToAll) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    server.start();

    int c1 = connect_client(port_);
    ASSERT_GE(c1, 0);
    server.poll_once(50);
    int c2 = connect_client(port_);
    ASSERT_GE(c2, 0);
    server.poll_once(50);

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    server.write_to_all(payload);

    EXPECT_EQ(recv_all(c1, payload.size()), payload);
    EXPECT_EQ(recv_all(c2, payload.size()), payload);

    ::close(c1);
    ::close(c2);
    server.stop();
}

TEST_F(TcpOutputServerTest, WritePreservesOrder) {
    qpsk_b200::TcpOutputServer server("127.0.0.1", port_);
    server.start();
    int client_fd = connect_client(port_);
    ASSERT_GE(client_fd, 0);
    server.poll_once(50);

    server.write_to_all({0x01, 0x02});
    server.write_to_all({0x03, 0x04});
    server.write_to_all({0x05, 0x06});

    auto received = recv_all(client_fd, 6);
    EXPECT_EQ(received, (std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}));

    ::close(client_fd);
    server.stop();
}

// ===========================================================================
// Bind failure tests
// ===========================================================================

TEST(TcpServerBindFailure, InputServerBindFailure) {
    uint16_t port = random_port();
    qpsk_b200::TcpInputServer s1("127.0.0.1", port, 64);
    s1.start();
    qpsk_b200::TcpInputServer s2("127.0.0.1", port, 64);
    EXPECT_THROW(s2.start(), std::runtime_error);
    s1.stop();
}

TEST(TcpServerBindFailure, OutputServerBindFailure) {
    uint16_t port = random_port();
    qpsk_b200::TcpOutputServer s1("127.0.0.1", port);
    s1.start();
    qpsk_b200::TcpOutputServer s2("127.0.0.1", port);
    EXPECT_THROW(s2.start(), std::runtime_error);
    s1.stop();
}
