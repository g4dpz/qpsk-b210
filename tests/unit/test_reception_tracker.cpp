#include "ltp_cla/reception_tracker.h"

#include <gtest/gtest.h>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Single range
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, SingleRange) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 100u);
}

// ---------------------------------------------------------------------------
// Two non-overlapping ranges
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, TwoDisjointRanges) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    rt.add_range(200, 100);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 2u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 100u);
    EXPECT_EQ(claims[1].offset, 200u);
    EXPECT_EQ(claims[1].length, 100u);
}

// ---------------------------------------------------------------------------
// Merge adjacent ranges
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, MergeAdjacent) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    rt.add_range(100, 100);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 200u);
}

TEST(ReceptionTrackerTest, MergeAdjacentReverse) {
    ReceptionTracker rt;
    rt.add_range(100, 100);
    rt.add_range(0, 100);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 200u);
}

// ---------------------------------------------------------------------------
// Merge overlapping ranges
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, MergeOverlapping) {
    ReceptionTracker rt;
    rt.add_range(0, 150);
    rt.add_range(100, 150);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 250u);
}

TEST(ReceptionTrackerTest, MergeContained) {
    ReceptionTracker rt;
    rt.add_range(0, 300);
    rt.add_range(50, 100);  // entirely within existing range
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 300u);
}

TEST(ReceptionTrackerTest, MergeSpanningMultiple) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    rt.add_range(200, 100);
    rt.add_range(400, 100);
    // Now add a range that spans the first two
    rt.add_range(50, 200);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 2u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 300u);
    EXPECT_EQ(claims[1].offset, 400u);
    EXPECT_EQ(claims[1].length, 100u);
}

// ---------------------------------------------------------------------------
// Out-of-order insertion
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, OutOfOrderInsertion) {
    ReceptionTracker rt;
    rt.add_range(200, 100);
    rt.add_range(0, 100);
    rt.add_range(100, 100);
    auto claims = rt.get_claims();
    ASSERT_EQ(claims.size(), 1u);
    EXPECT_EQ(claims[0].offset, 0u);
    EXPECT_EQ(claims[0].length, 300u);
}

// ---------------------------------------------------------------------------
// Zero-length range (no-op)
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, ZeroLengthRange) {
    ReceptionTracker rt;
    rt.add_range(0, 0);
    EXPECT_TRUE(rt.get_claims().empty());
}

// ---------------------------------------------------------------------------
// Completeness check
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, IsComplete_True) {
    ReceptionTracker rt;
    rt.add_range(0, 1400);
    EXPECT_TRUE(rt.is_complete(1400));
}

TEST(ReceptionTrackerTest, IsComplete_False_Gap) {
    ReceptionTracker rt;
    rt.add_range(0, 700);
    rt.add_range(800, 600);
    EXPECT_FALSE(rt.is_complete(1400));
}

TEST(ReceptionTrackerTest, IsComplete_False_Empty) {
    ReceptionTracker rt;
    EXPECT_FALSE(rt.is_complete(100));
}

TEST(ReceptionTrackerTest, IsComplete_ZeroLength) {
    ReceptionTracker rt;
    EXPECT_TRUE(rt.is_complete(0));
}

TEST(ReceptionTrackerTest, IsComplete_OverReceived) {
    ReceptionTracker rt;
    rt.add_range(0, 2000);
    EXPECT_TRUE(rt.is_complete(1400));
}

// ---------------------------------------------------------------------------
// Gap computation
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, Gaps_FullGap) {
    ReceptionTracker rt;
    auto gaps = rt.get_gaps(0, 1400);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].offset, 0u);
    EXPECT_EQ(gaps[0].length, 1400u);
}

TEST(ReceptionTrackerTest, Gaps_NoGap) {
    ReceptionTracker rt;
    rt.add_range(0, 1400);
    auto gaps = rt.get_gaps(0, 1400);
    EXPECT_TRUE(gaps.empty());
}

TEST(ReceptionTrackerTest, Gaps_MiddleGap) {
    ReceptionTracker rt;
    rt.add_range(0, 500);
    rt.add_range(800, 600);
    auto gaps = rt.get_gaps(0, 1400);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].offset, 500u);
    EXPECT_EQ(gaps[0].length, 300u);
}

TEST(ReceptionTrackerTest, Gaps_MultipleGaps) {
    ReceptionTracker rt;
    rt.add_range(100, 100);
    rt.add_range(300, 100);
    auto gaps = rt.get_gaps(0, 500);
    ASSERT_EQ(gaps.size(), 3u);
    EXPECT_EQ(gaps[0].offset, 0u);
    EXPECT_EQ(gaps[0].length, 100u);
    EXPECT_EQ(gaps[1].offset, 200u);
    EXPECT_EQ(gaps[1].length, 100u);
    EXPECT_EQ(gaps[2].offset, 400u);
    EXPECT_EQ(gaps[2].length, 100u);
}

TEST(ReceptionTrackerTest, Gaps_ScopedQuery) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    rt.add_range(200, 100);
    // Query only within [50, 250)
    auto gaps = rt.get_gaps(50, 250);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].offset, 100u);
    EXPECT_EQ(gaps[0].length, 100u);
}

// ---------------------------------------------------------------------------
// total_received
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, TotalReceived) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    rt.add_range(200, 100);
    EXPECT_EQ(rt.total_received(), 200u);
    rt.add_range(100, 100);  // fills the gap
    EXPECT_EQ(rt.total_received(), 300u);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST(ReceptionTrackerTest, Clear) {
    ReceptionTracker rt;
    rt.add_range(0, 100);
    rt.clear();
    EXPECT_TRUE(rt.get_claims().empty());
    EXPECT_FALSE(rt.is_complete(100));
}
