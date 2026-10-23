#pragma once

// Identifies the byte range containing a value in a numbered log file.

#include <cstdint>

struct Entry {
    uint32_t file_id;
    uint64_t value_pos;
    uint32_t value_size;
};
