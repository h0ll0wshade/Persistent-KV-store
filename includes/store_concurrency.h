#pragma once

// Provides shared read access and exclusive write access around store calls.

#include <mutex>
#include <shared_mutex>
#include <utility>

// Synchronization boundary for KVStore operations and close-time lifetime.
class StoreConcurrency {
public:
    template <typename Function>
    decltype(auto) withRead(Function&& function) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return std::forward<Function>(function)();
    }

    template <typename Function>
    decltype(auto) withWrite(Function&& function) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        return std::forward<Function>(function)();
    }

private:
    mutable std::shared_mutex mutex;
};
