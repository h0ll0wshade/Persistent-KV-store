#pragma once

#include <cstdint>

struct Options {
    uint64_t max_file_size = 64ULL * 1024ULL * 1024ULL;
    // Sync each successful mutation (PUT or DELETE tombstone) before returning.
    bool sync_on_put = false;
    // Open existing files without creating or modifying database data.
    bool read_only = false;
};
