#include "ltp_cla/tcp_ingress.h"
#include "ltp_cla/convergence_layer_adapter.h"  // for frame_message

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
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

// Helper: write all bytes
static bool write_all(int fd, const std::vector<uint8_t>& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n <= 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Accept connection and receive framed data
// ---------------------------------------------------------------------------

TEST(TcpIngressTest, AcceptAndReceiveData) {
    IngressConfig cfg;
    cfg.bind_addr = "127.0.0.1";
    cfg.port = 0;  // OS-assigned port
    TcpIngress ingress(cfg);

    std::vector<std::vector<uint8_t>> received;
    std::mutex mtx;
    ingress.set_data_callback([&](std::vector<uint8_t> data) {
        std::lock_guard<std::mutex> lock(mtx);
        received.push_back(std::move(data));
    });

    ingress.start();
    uint16_t port = ingress.listening_port();
    ASSERT_GT(port, 0);

    // Connect and send a framed message
    int client = connect_local(port);
    ASSERT_GE(client, 0);

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto framed = frame_message(payload);
    ASSERT_TRUE(write_all(client, framed));

    // Wait for delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_EQ(received.size(), 1u);
        EXPECT_EQ(received[0], payload);
    }

    ::close(client);
    ingress.stop();
}

// ---------------------------------------------------------------------------
// Multiple data blocks from a single client
// ---------------------------------------------------------------------------

TEST(TcpIngressTest, MultipleBlocks) {
    IngressConfig cfg;
    cfg.port = 0;
    TcpIngress ingress(cfg);

    std::vector<std::vector<uint8_t>> received;
    std::mutex mtx;
    ingress.set_data_callback([&](std::vector<uint8_t> data) {
        std::lock_guard<std::mutex> lock(mtx);
        received.push_back(std::move(data));
    });

    ingress.start();
    int client = connect_local(ingress.listening_port());
    ASSERT_GE(client, 0);

    // Send 3 framed messages
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> payload(50, static_cast<uint8_t>(i));
        auto framed = frame_message(payload);
        ASSERT_TRUE(write_all(client, framed));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_EQ(received.size(), 3u);
        for (int i = 0; i < 3; ++i) {
            EXPECT_EQ(received[static_cast<size_t>(i)].size(), 50u);
            EXPECT_EQ(received[static_cast<size_t>(i)][0], static_cast<uint8_t>(i));
        }
    }

    ::close(client);
    ingress.stop();
}

// ---------------------------------------------------------------------------
// Multiple simultaneous clients
// ---------------------------------------------------------------------------

TEST(TcpIngressTest, MultipleClients) {
    IngressConfig cfg;
    cfg.port = 0;
    TcpIngress ingress(cfg);

    std::atomic<int> count{0};
    ingress.set_data_callback([&](std::vector<uint8_t>) {
        count++;
    });

    ingress.start();
    uint16_t port = ingress.listening_port();

    int c1 = connect_local(port);
    int c2 = connect_local(port);
    ASSERT_GE(c1, 0);
    ASSERT_GE(c2, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(ingress.client_count(), 2u);

    // Each client sends one message
    std::vector<uint8_t> p1 = {0xAA};
    std::vector<uint8_t> p2 = {0xBB};
    write_all(c1, frame_message(p1));
    write_all(c2, frame_message(p2));

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(count.load(), 2);

    ::close(c1);
    ::close(c2);
    ingress.stop();
}

// ---------------------------------------------------------------------------
// Client disconnect mid-frame — partial data discarded
// ---------------------------------------------------------------------------

TEST(TcpIngressTest, ClientDisconnectMidFrame) {
    IngressConfig cfg;
    cfg.port = 0;
    TcpIngress ingress(cfg);

    std::atomic<int> count{0};
    ingress.set_data_callback([&](std::vector<uint8_t>) {
        count++;
    });

    ingress.start();
    int client = connect_local(ingress.listening_port());
    ASSERT_GE(client, 0);

    // Send partial frame (length says 100 bytes but only send 10)
    std::vector<uint8_t> partial = {0x00, 0x00, 0x00, 0x64,
                                     0x01, 0x02, 0x03, 0x04, 0x05,
                                     0x06, 0x07, 0x08, 0x09, 0x0A};
    write_all(client, partial);
    ::close(client);  // disconnect before completing the frame

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // No complete message should have been delivered
    EXPECT_EQ(count.load(), 0);

    ingress.stop();
}

// ---------------------------------------------------------------------------
// Bind failure (port already in use)
// ---------------------------------------------------------------------------

TEST(TcpIngressTest, BindFailure) {
    IngressConfig cfg;
    cfg.port = 0;
    TcpIngress ingress1(cfg);
    ingress1.start();
    uint16_t port = ingress1.listening_port();
    ASSERT_GT(port, 0);

    // Try to bind a second server to the same port
    IngressConfig cfg2;
    cfg2.port = port;
    TcpIngress ingress2(cfg2);
    ingress2.start();

    // Second server should fail to start (listening_port stays 0 or
    // the start silently fails — either way it shouldn't crash)
    // Just verify no crash
    ingress2.stop();
    ingress1.stop();
}
