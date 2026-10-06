#pragma once

#include <cstdint>
#include <string>

enum class RecordType : uint8_t {
    Put = 1,
    Delete = 2
};

struct RecordHeader {
    uint32_t key_size;
    uint32_t value_size;
    uint8_t type;
};

struct Record {
    RecordHeader header;
    std::string key;
    std::string value;
};