// Feature: bp-ltp-dtn, Property 17: Reception Tracker Merge Invariant
//
// For any sequence of add_range operations, the resulting claims list SHALL
// contain no overlapping or adjacent ranges (all ranges are merged), and the
// union of all claims SHALL equal the union of all added ranges.
//
// **Validates: Requirement 6.5**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "ltp_cla/reception_tracker.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Generator: a sequence of (offset, length) pairs
// ---------------------------------------------------------------------------

struct RangeInput {
    uint64_t offset;
    uint64_t length;
};

static rc::Gen<RangeInput> genRange() {
    return rc::gen::map(
        rc::gen::tuple(
            rc::gen::inRange<uint64_t>(0, 10000),
            rc::gen::inRange<uint64_t>(0, 5000)),
        [](const auto& t) {
            return RangeInput{std::get<0>(t), std::get<1>(t)};
        });
}

// ---------------------------------------------------------------------------
// Helper: compute the set of all covered byte positions from raw inputs
// (for small ranges only — used for verification)
// ---------------------------------------------------------------------------

static std::set<uint64_t> covered_bytes(const std::vector<RangeInput>& inputs) {
    std::set<uint64_t> result;
    for (const auto& r : inputs) {
        // Limit to avoid huge sets in property tests
        if (r.length > 2000) continue;
        for (uint64_t i = 0; i < r.length; ++i) {
            result.insert(r.offset + i);
        }
    }
    return result;
}

static std::set<uint64_t> covered_bytes_from_claims(
    const std::vector<ReceptionClaim>& claims) {
    std::set<uint64_t> result;
    for (const auto& c : claims) {
        if (c.length > 2000) continue;
        for (uint64_t i = 0; i < c.length; ++i) {
            result.insert(c.offset + i);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Property 17a: No overlapping or adjacent ranges after merges
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ReceptionTrackerProperty17, NoOverlappingOrAdjacentRanges, ()) {
    auto inputs = *rc::gen::container<std::vector<RangeInput>>(genRange());
    // Limit number of inputs to keep test fast
    if (inputs.size() > 50) inputs.resize(50);

    ReceptionTracker rt;
    for (const auto& r : inputs) {
        rt.add_range(r.offset, r.length);
    }

    const auto& claims = rt.get_claims();

    // Check sorted by offset
    for (size_t i = 1; i < claims.size(); ++i) {
        RC_ASSERT(claims[i].offset > claims[i - 1].offset);
    }

    // Check no overlapping or adjacent ranges
    for (size_t i = 1; i < claims.size(); ++i) {
        uint64_t prev_end = claims[i - 1].offset + claims[i - 1].length;
        // Strictly less than (not equal — equal would mean adjacent)
        RC_ASSERT(prev_end < claims[i].offset);
    }

    // Check no zero-length claims
    for (const auto& c : claims) {
        RC_ASSERT(c.length > 0);
    }
}

// ---------------------------------------------------------------------------
// Property 17b: Union of claims equals union of inputs
// (only for small ranges to avoid memory explosion)
// ---------------------------------------------------------------------------

RC_GTEST_PROP(ReceptionTrackerProperty17, UnionPreserved, ()) {
    auto inputs = *rc::gen::container<std::vector<RangeInput>>(
        rc::gen::map(
            rc::gen::tuple(
                rc::gen::inRange<uint64_t>(0, 500),
                rc::gen::inRange<uint64_t>(0, 200)),
            [](const auto& t) {
                return RangeInput{std::get<0>(t), std::get<1>(t)};
            }));
    if (inputs.size() > 20) inputs.resize(20);

    ReceptionTracker rt;
    for (const auto& r : inputs) {
        rt.add_range(r.offset, r.length);
    }

    auto expected = covered_bytes(inputs);
    auto actual = covered_bytes_from_claims(rt.get_claims());

    RC_ASSERT(expected == actual);
}
