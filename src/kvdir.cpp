#include "kvdir.h"

std::optional<Entry> KeyDir::get(const std::string& key) const {
    const auto it = kv.find(key);
    if (it == kv.end()) return std::nullopt;
    return it->second;
}

void KeyDir::put(const std::string& key, const Entry& entry) {
    kv[key] = entry;
}

void KeyDir::erase(const std::string& key) {
    kv.erase(key);
}

std::vector<std::string> KeyDir::listKeys() const {
    std::vector<std::string> keys;
    keys.reserve(kv.size());
    for (const auto& item : kv) keys.push_back(item.first);
    return keys;
}
