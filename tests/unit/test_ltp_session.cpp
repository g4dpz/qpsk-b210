#include "ltp_cla/ltp_session.h"

#include <gtest/gtest.h>
#include <unordered_map>

using namespace ltp;

// ---------------------------------------------------------------------------
// SessionKey equality and hashing
// ---------------------------------------------------------------------------

TEST(LtpSessionKeyTest, Equality) {
    SessionKey a{1, 42};
    SessionKey b{1, 42};
    SessionKey c{1, 43};
    SessionKey d{2, 42};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
}

TEST(LtpSessionKeyTest, HashMapLookup) {
    std::unordered_map<SessionKey, int, SessionKeyHash> map;
    SessionKey k1{1, 10};
    SessionKey k2{1, 20};
    SessionKey k3{2, 10};
    map[k1] = 100;
    map[k2] = 200;
    map[k3] = 300;

    EXPECT_EQ(map[k1], 100);
    EXPECT_EQ(map[k2], 200);
    EXPECT_EQ(map[k3], 300);
    SessionKey k4{3, 10};
    EXPECT_EQ(map.count(k4), 0u);
}

// ---------------------------------------------------------------------------
// Session defaults
// ---------------------------------------------------------------------------

TEST(LtpSessionTest, Defaults) {
    LtpSession s;
    EXPECT_EQ(s.state, SessionState::ACTIVE);
    EXPECT_EQ(s.role, SessionRole::ORIGINATOR);
    EXPECT_TRUE(s.is_red);
    EXPECT_EQ(s.total_data_length, 0u);
    EXPECT_EQ(s.checkpoint_serial_counter, 0u);
    EXPECT_EQ(s.report_serial_counter, 0u);
    EXPECT_EQ(s.retransmission_count, 0u);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

TEST(LtpSessionTest, StateTransitions) {
    LtpSession s;
    EXPECT_EQ(s.state, SessionState::ACTIVE);

    s.state = SessionState::COMPLETED;
    EXPECT_EQ(s.state, SessionState::COMPLETED);

    LtpSession s2;
    s2.state = SessionState::CANCELLED;
    EXPECT_EQ(s2.state, SessionState::CANCELLED);
}
