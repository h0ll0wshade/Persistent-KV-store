#pragma once

#include "entry.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class KeyDir {
private:
    std::unordered_map<std::string, Entry> kv;

public:
    std::optional<Entry> get(const std::string& key) const;

    void put(const std::string& key, const Entry& entry);

    void erase(const std::string& key);

    std::vector<std::string> listKeys() const;

    // Recovery functionality will be implemented later.
    // void rebuild(...);
};
