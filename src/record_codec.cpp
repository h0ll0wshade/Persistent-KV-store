#include "record_codec.h"

#include <cstring>
#include <limits>

namespace {
constexpr uint8_t kMagic[4] = {'B', 'K', 'V', 'R'};
constexpr size_t kCrcOffset = 16;

uint32_t readU32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

void writeU32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

uint32_t crc32(const uint8_t* bytes, size_t size, uint32_t crc = 0xffffffffU) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}

uint32_t checksum(const uint8_t* bytes, size_t size) {
    uint32_t crc = crc32(bytes, kCrcOffset);
    crc = crc32(bytes + RecordCodec::kHeaderSize, size - RecordCodec::kHeaderSize, crc);
    return crc ^ 0xffffffffU;
}

void validateHeader(const uint8_t* bytes, size_t size) {
    if (size < RecordCodec::kHeaderSize) throw RecordFormatError("Truncated record header");
    if (std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0)
        throw RecordFormatError("Invalid record magic");
    if (bytes[4] != RecordCodec::kVersion) throw RecordFormatError("Unsupported record version");
    if (bytes[5] != static_cast<uint8_t>(RecordType::Put) &&
        bytes[5] != static_cast<uint8_t>(RecordType::Delete))
        throw RecordFormatError("Invalid record type");
    if (bytes[6] != 0 || bytes[7] != 0) throw RecordFormatError("Invalid reserved record bits");
    if (bytes[5] == static_cast<uint8_t>(RecordType::Delete) && readU32(bytes + 12) != 0)
        throw RecordFormatError("Delete record has a value");
}
}

uint64_t RecordCodec::encodedSize(const Record& record) {
    if (record.key.size() > std::numeric_limits<uint32_t>::max() ||
        record.value.size() > std::numeric_limits<uint32_t>::max())
        throw std::length_error("Record field exceeds the on-disk size limit");
    if (record.header.type != static_cast<uint8_t>(RecordType::Put) &&
        record.header.type != static_cast<uint8_t>(RecordType::Delete))
        throw std::invalid_argument("Unknown record type");
    if (record.header.type == static_cast<uint8_t>(RecordType::Delete) && !record.value.empty())
        throw std::invalid_argument("Delete record must not contain a value");
    return kHeaderSize + static_cast<uint64_t>(record.key.size()) + record.value.size();
}

std::vector<uint8_t> RecordCodec::encode(const Record& record) {
    const uint64_t total_size = encodedSize(record);
    std::vector<uint8_t> bytes(static_cast<size_t>(total_size), 0);
    std::memcpy(bytes.data(), kMagic, sizeof(kMagic));
    bytes[4] = kVersion;
    bytes[5] = record.header.type;
    writeU32(bytes.data() + 8, static_cast<uint32_t>(record.key.size()));
    writeU32(bytes.data() + 12, static_cast<uint32_t>(record.value.size()));
    if (!record.key.empty())
        std::memcpy(bytes.data() + kHeaderSize, record.key.data(), record.key.size());
    if (!record.value.empty())
        std::memcpy(bytes.data() + kHeaderSize + record.key.size(),
                    record.value.data(), record.value.size());
    writeU32(bytes.data() + kCrcOffset, checksum(bytes.data(), bytes.size()));
    return bytes;
}

uint64_t RecordCodec::encodedSizeFromHeader(const uint8_t* bytes, size_t size) {
    validateHeader(bytes, size);
    return kHeaderSize + static_cast<uint64_t>(readU32(bytes + 8)) + readU32(bytes + 12);
}

Record RecordCodec::decode(const uint8_t* bytes, size_t size) {
    const uint64_t expected_size = encodedSizeFromHeader(bytes, size);
    if (expected_size != size) throw RecordFormatError("Record size does not match its header");
    if (readU32(bytes + kCrcOffset) != checksum(bytes, size))
        throw RecordFormatError("Record checksum mismatch");

    RecordHeader header{};
    header.type = bytes[5];
    header.key_size = readU32(bytes + 8);
    header.value_size = readU32(bytes + 12);
    const char* body = reinterpret_cast<const char*>(bytes + kHeaderSize);
    return Record{header,
                  std::string(body, header.key_size),
                  std::string(body + header.key_size, header.value_size)};
}
