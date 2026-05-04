#pragma once

#include "ltp_cla/ltp_segment.h"  // for ReceptionClaim

#include <cstdint>
#include <vector>

namespace ltp {

/// Tracks received byte ranges for an LTP receiver session.
///
/// Maintains a sorted, non-overlapping list of (offset, length) pairs.
/// Automatically merges overlapping and adjacent ranges on insertion.
class ReceptionTracker {
public:
    /// Record that bytes [offset, offset+length) have been received.
    /// Merges with any overlapping or adjacent existing ranges.
    void add_range(uint64_t offset, uint64_t length);

    /// Returns true if the received ranges cover [0, total_length) completely.
    bool is_complete(uint64_t total_length) const;

    /// Returns the gaps (unreceived ranges) within [lower, upper).
    std::vector<ReceptionClaim> get_gaps(uint64_t lower, uint64_t upper) const;

    /// Returns all received ranges as a list of claims.
    const std::vector<ReceptionClaim>& get_claims() const { return ranges_; }

    /// Returns the total number of bytes received (sum of all range lengths).
    uint64_t total_received() const;

    /// Clears all tracked ranges.
    void clear() { ranges_.clear(); }

private:
    // Sorted by offset, non-overlapping, non-adjacent.
    std::vector<ReceptionClaim> ranges_;
};

}  // namespace ltp
