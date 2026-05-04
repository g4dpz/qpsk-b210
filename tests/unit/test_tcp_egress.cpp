#include "ltp_cla/tcp_egress.h"
#include "ltp_cla/convergence_layer_adapter.h"  // for extract_framed_message

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include <chrono>
#include <thread>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Helper: connect to a local TCP server
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

// Helper: read all available data from a socket with timeout
static std::vector<uint8_t> read_all(int fd, size_t expected, int timeout_ms = 500) {
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
// Deliver to a connected client
// ---------------------------------------------------------------------------

TEST(TcpEgressTest, DeliverToClient) {
    EgressConfig cfg;
    cfg.port = 0;
    TcpEgress egress(cfg);
    egress.start();

    int client = connect_local(egress.listening_port());
    ASSERT_GE(client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    egress.deliver(payload);

    // Read the framed message: 4-byte length + 4 data bytes = 8 bytes
    auto received = read_all(client, 8);
    ASSERT_EQ(received.size(), 8u);

    std::vector<uint8_t> extracted;
    ASSERT_TRUE(extract_framed_message(received, extracted));
    EXPECT_EQ(extracted, payload);

    ::close(client);
    egress.stop();
}

// ---------------------------------------------------------------------------
// Deliver to multiple clients
// ---------------------------------------------------------------------------

TEST(TcpEgressTest, DeliverToMultipleClients) {
    EgressConfig cfg;
    cfg.port = 0;
    TcpEgress egress(cfg);
    egress.start();

    int c1 = connect_local(egress.listening_port());
    int c2 = connect_local(egress.listening_port());
    ASSERT_GE(c1, 0);
    ASSERT_GE(c2, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<uint8_t> payload = {0xAA, 0xBB};
    egress.deliver(payload);

    auto r1 = read_all(c1, 6);
    auto r2 = read_all(c2, 6);

    std::vector<uint8_t> e1, e2;
    ASSERT_TRUE(extract_framed_message(r1, e1));
    ASSERT_TRUE(extract_framed_message(r2, e2));
    EXPECT_EQ(e1, payload);
    EXPECT_EQ(e2, payload);

    ::close(c1);
    ::close(c2);
    egress.stop();
}

// ---------------------------------------------------------------------------
// Buffer when no clients connected, deliver on connect
// ---------------------------------------------------------------------------

TEST(TcpEgressTest, BufferAndDeliverOnConnect) {
    EgressConfig cfg;
    cfg.port = 0;
    TcpEgress egress(cfg);
    egress.start();

    // Deliver before any client connects
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    egress.deliver(payload);

    EXPECT_GT(egress.buffer_bytes(), 0u);

    // Now connect a client
    int client = connect_local(egress.listening_port());
    ASSERT_GE(client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Should receive the buffered data
    auto received = read_all(client, 7);  // 4 + 3
    std::vector<uint8_t> extracted;
    ASSERT_TRUE(extract_framed_message(received, extracted));
    EXPECT_EQ(extracted, payload);

    // Buffer should be empty now
    EXPECT_EQ(egress.buffer_bytes(), 0u);

    ::close(client);
    egress.stop();
}

// ---------------------------------------------------------------------------
// Buffer overflow — oldest discarded
// ---------------------------------------------------------------------------

TEST(TcpEgressTest, BufferOverflow) {
    EgressConfig cfg;
    cfg.port = 0;
    cfg.max_buffer_bytes = 50;  // very small buffer
    TcpEgress egress(cfg);
    egress.start();

    // Deliver several blocks that exceed the buffer
    for (int i = 0; i < 10; ++i) {
        std::vector<uint8_t> payload(10, static_cast<uint8_t>(i));
        egress.deliver(payload);
    }

    // Buffer should not exceed max
    EXPECT_LE(egress.buffer_bytes(), cfg.max_buffer_bytes + 20);
    // (allow some slack for the framing overhead of the last message)

    egress.stop();
}

// ---------------------------------------------------------------------------
// Framing round-trip through egress
// ---------------------------------------------------------------------------

TEST(TcpEgressTest, FramingRoundTrip) {
    EgressConfig cfg;
    cfg.port = 0;
    TcpEgress egress(cfg);
    egress.start();

    int client = connect_local(egress.listening_port());
    ASSERT_GE(client, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Deliver multiple messages
    std::vector<uint8_t> p1 = {0xAA};
    std::vector<uint8_t> p2 = {0xBB, 0xCC, 0xDD};
    egress.deliver(p1);
    egress.deliver(p2);

    auto received = read_all(client, 5 + 7);  // (4+1) + (4+3)

    std::vector<uint8_t> e1, e2;
    ASSERT_TRUE(extract_framed_message(received, e1));
    ASSERT_TRUE(extract_framed_message(received, e2));
    EXPECT_EQ(e1, p1);
    EXPECT_EQ(e2, p2);

    ::close(client);
    egress.stop();
}
