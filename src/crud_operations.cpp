#include "crud_operations.h"

#include "records.h"

namespace {
Record makeRecord(RecordType type, const std::string& key, const std::string& value) {
    Record record{};
    record.header.type = static_cast<uint8_t>(type);
    record.key = key;
    record.value = value;
    return record;
}
}

CrudOperations::CrudOperations(KeyDir& index, LogFile& log)
    : keydir(index), active_log(log) {}

void CrudOperations::put(const std::string& key, const std::string& value) {
    const Entry entry = active_log.append(makeRecord(RecordType::Put, key, value));
    keydir.put(key, entry);
}

std::optional<std::string> CrudOperations::get(const std::string& key) const {
    const auto entry = keydir.get(key);
    if (!entry) return std::nullopt;
    return active_log.read(*entry);
}

void CrudOperations::erase(const std::string& key) {
    active_log.append(makeRecord(RecordType::Delete, key, std::string{}));
    keydir.erase(key);
}

std::vector<std::string> CrudOperations::listKeys() const {
    return keydir.listKeys();
}

void CrudOperations::sync() {
    active_log.sync();
}
