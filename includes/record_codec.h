#pragma once

#include "records.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

class RecordFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Portable on-disk representation for records. RecordHeader remains an
// in-memory description and is never written as a native struct.
class RecordCodec {
public:
    static constexpr size_t kHeaderSize = 20;
    static constexpr uint8_t kVersion = 1;

    static uint64_t encodedSize(const Record& record);
    static std::vector<uint8_t> encode(const Record& record);
    static uint64_t encodedSizeFromHeader(const uint8_t* bytes, size_t size);
    static Record decode(const uint8_t* bytes, size_t size);
};
