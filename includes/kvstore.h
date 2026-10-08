#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

class KVStore {
private:
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    KVStore(const std::string& directory);
    ~KVStore();

    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;
    KVStore(KVStore&&) = delete;
    KVStore& operator=(KVStore&&) = delete;

    void put(const std::string& key, const std::string& value);

    std::optional<std::string> get(const std::string& key) const;

    void erase(const std::string& key);

    std::vector<std::string> listKeys() const;
    void sync();
};
