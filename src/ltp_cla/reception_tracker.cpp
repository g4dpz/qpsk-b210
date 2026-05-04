#include "ltp_cla/reception_tracker.h"

#include <algorithm>

namespace ltp {

// ---------------------------------------------------------------------------
// add_range — insert [offset, offset+length), merge overlapping/adjacent
// ---------------------------------------------------------------------------

void ReceptionTracker::add_range(uint64_t offset, uint64_t length) {
    if (length == 0) return;

    uint64_t new_start = offset;
    uint64_t new_end = offset + length;

    // Find the first range that could overlap or be adjacent (end >= new_start).
    // We scan from the beginning since ranges are sorted by offset.
    auto it = ranges_.begin();

    // Skip ranges that end strictly before new_start (no overlap, no adjacency).
    while (it != ranges_.end() && (it->offset + it->length) < new_start) {
        ++it;
    }

    // Merge with all ranges that overlap or are adjacent.
    while (it != ranges_.end() && it->offset <= new_end) {
        new_start = std::min(new_start, it->offset);
        new_end = std::max(new_end, it->offset + it->length);
        it = ranges_.erase(it);
    }

    // Insert the merged range at the correct position.
    ReceptionClaim merged;
    merged.offset = new_start;
    merged.length = new_end - new_start;
    ranges_.insert(it, merged);
}

// ---------------------------------------------------------------------------
// is_complete — check if [0, total_length) is fully covered
// ---------------------------------------------------------------------------

bool ReceptionTracker::is_complete(uint64_t total_length) const {
    if (total_length == 0) return true;
    if (ranges_.empty()) return false;

    // After merging, completeness means a single range [0, total_length).
    return ranges_.size() == 1 &&
           ranges_[0].offset == 0 &&
           ranges_[0].length >= total_length;
}

// ---------------------------------------------------------------------------
// get_gaps — return unreceived ranges within [lower, upper)
// ---------------------------------------------------------------------------

std::vector<ReceptionClaim> ReceptionTracker::get_gaps(uint64_t lower,
                                                        uint64_t upper) const {
    std::vector<ReceptionClaim> gaps;
    if (lower >= upper) return gaps;

    uint64_t cursor = lower;

    for (const auto& r : ranges_) {
        if (cursor >= upper) break;

        uint64_t r_end = r.offset + r.length;

        // Skip ranges entirely before the cursor.
        if (r_end <= cursor) continue;

        // If there's a gap before this range, record it.
        if (r.offset > cursor) {
            uint64_t gap_end = std::min(r.offset, upper);
            gaps.push_back({cursor, gap_end - cursor});
        }

        // Advance cursor past this range.
        cursor = std::max(cursor, r_end);
    }

    // If there's a gap after the last range, record it.
    if (cursor < upper) {
        gaps.push_back({cursor, upper - cursor});
    }

    return gaps;
}

// ---------------------------------------------------------------------------
// total_received — sum of all range lengths
// ---------------------------------------------------------------------------

uint64_t ReceptionTracker::total_received() const {
    uint64_t total = 0;
    for (const auto& r : ranges_) {
        total += r.length;
    }
    return total;
}

}  // namespace ltp
