// Tests record encoding, decoding, format validation, and checksum rejection.
#include "record_codec.h"
#include "test_support.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
bool rejected(const std::vector<uint8_t>& bytes) {
    try {
        (void)RecordCodec::decode(bytes.data(), bytes.size());
    } catch (const RecordFormatError&) {
        return true;
    }
    return false;
}
}

int main() {
    Record input{};
    input.header.type = static_cast<uint8_t>(RecordType::Put);
    input.key = "codec-key";
    input.value = "value with spaces";
    const auto encoded = RecordCodec::encode(input);
    check(encoded.size() == RecordCodec::kHeaderSize + input.key.size() + input.value.size(),
          "encoded record should use fixed header plus payload bytes");
    const auto decoded = RecordCodec::decode(encoded.data(), encoded.size());
    check(decoded.header.type == input.header.type && decoded.key == input.key &&
              decoded.value == input.value,
          "versioned record should round-trip");

    auto checksum_error = encoded;
    checksum_error.back() ^= 0x40;
    check(rejected(checksum_error), "payload corruption should fail its checksum");

    auto version_error = encoded;
    version_error[4] = 2;
    check(rejected(version_error), "unsupported record versions should be rejected");

    auto type_error = encoded;
    type_error[5] = 77;
    check(rejected(type_error), "unsupported record types should be rejected");

    auto reserved_error = encoded;
    reserved_error[6] = 1;
    check(rejected(reserved_error), "nonzero reserved bits should be rejected");

    check(rejected(std::vector<uint8_t>(encoded.begin(), encoded.begin() + 7)),
          "truncated headers should be rejected");

    Record tombstone{};
    tombstone.header.type = static_cast<uint8_t>(RecordType::Delete);
    tombstone.key = "deleted";
    const auto encoded_tombstone = RecordCodec::encode(tombstone);
    const auto decoded_tombstone = RecordCodec::decode(encoded_tombstone.data(), encoded_tombstone.size());
    check(decoded_tombstone.header.type == static_cast<uint8_t>(RecordType::Delete) &&
              decoded_tombstone.value.empty(),
          "delete records should round-trip without a value");

    bool invalid_delete_rejected = false;
    tombstone.value = "not-empty";
    try { (void)RecordCodec::encode(tombstone); }
    catch (const std::invalid_argument&) { invalid_delete_rejected = true; }
    check(invalid_delete_rejected, "delete records with a value should be rejected");
}
