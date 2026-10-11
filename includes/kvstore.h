#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "options.h"
#include "store_concurrency.h"

class KVStore {
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    mutable StoreConcurrency concurrency;

public:
    explicit KVStore(const std::string& directory, const Options& options = {});
    ~KVStore();

    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;
    KVStore(KVStore&&) = delete;
    KVStore& operator=(KVStore&&) = delete;

    void put(const std::string& key, const std::string& value);

    std::optional<std::string> get(const std::string& key) const;

    void erase(const std::string& key);

    std::vector<std::string> listKeys() const;
    // Flush all log files and database-directory metadata.
    void sync();
    void compact();
    void close();
};
