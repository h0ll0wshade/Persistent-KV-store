#pragma once

// Declares the public, thread-safe API for opening and operating the store.

#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "options.h"
#include "kv_stats.h"
#include "store_concurrency.h"

class KVStore {
private:
    struct Impl;
    mutable StoreConcurrency concurrency;
    mutable std::mutex close_mutex;
    std::unique_ptr<Impl> impl;

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
    void fold(const std::function<void(const std::string&, const std::string&)>& callback) const;
    KVStats stats() const;
};
