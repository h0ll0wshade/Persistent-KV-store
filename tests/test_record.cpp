#include "logfile.h"
#include "test_support.h"

#include <cstdint>

int main() {
    TempDirectory directory;
    LogFile file(directory.path() + "/record.log", 7, true);
    Record input{};
    input.header.type = static_cast<uint8_t>(RecordType::Put);
    input.key = "alpha";
    input.value = "value";
    const Entry entry = file.append(input);
    check(entry.file_id == 7, "record should retain its file ID");
    check(entry.value_pos == sizeof(RecordHeader) + input.key.size(), "entry should point at value bytes");
    const auto decoded = file.readRecord(0);
    check(decoded.has_value(), "record should be readable");
    check(decoded->header.key_size == input.key.size(), "key size should be serialized");
    check(decoded->header.value_size == input.value.size(), "value size should be serialized");
    check(decoded->key == input.key && decoded->value == input.value, "record fields should round-trip");
    check(!file.readRecord(file.size()).has_value(), "EOF should return no record");
}
