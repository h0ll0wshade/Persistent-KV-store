#include "kvstore.h"
#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace {
size_t logCount(const std::string& directory) {
    size_t count = 0;
    for (const auto& item : std::filesystem::directory_iterator(directory))
        if (item.path().extension() == ".log") ++count;
    return count;
}
}

int main() {
    TempDirectory directory;
    Options options;
    options.max_file_size = 1;
    options.enable_background_compaction = true;
    {
        KVStore store(directory.path(), options);
        for (int version = 0; version < 12; ++version)
            store.put("same-key", "version-" + std::to_string(version));

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (logCount(directory.path()) > 2 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(logCount(directory.path()) <= 2,
              "background worker should compact obsolete versions after four immutable logs");
        check(store.get("same-key") == std::optional<std::string>("version-11"),
              "background compaction should preserve the latest live value");
        store.close();
    }
    KVStore recovered(directory.path());
    check(recovered.get("same-key") == std::optional<std::string>("version-11"),
          "background compaction output should survive restart");
}
