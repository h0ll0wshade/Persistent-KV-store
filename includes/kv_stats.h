#pragma once

// Holds aggregate log size, live-key, and reclaimable-space metrics.

#include <cstddef>
#include <cstdint>

struct KVStats {
    size_t file_count{0};
    uint64_t total_bytes{0};
    size_t live_key_count{0};
    uint64_t reclaimable_bytes{0};
};
