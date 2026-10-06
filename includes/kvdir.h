#pragma once

#include "entry.h"

#include <optional>
#include <string>
#include <unordered_map>

class KeyDir {
private:
    std::unordered_map<std::string, Entry> kv;

public:
    std::string get(const std::string& key) const;

    void post(const std::string& key, const Entry& entry);

    void del(const std::string& key);

    // Recovery functionality will be implemented later.
    // void rebuild(...);
};