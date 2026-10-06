#pragma once

#include <cstdint>

struct Entry {
    uint32_t file_id;
    uint64_t value_pos;
    uint32_t value_size;
};