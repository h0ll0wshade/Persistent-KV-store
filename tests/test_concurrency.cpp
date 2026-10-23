// Exercises concurrent reads and writes, then checks recovery of the results.
#include "kvstore.h"
#include "test_support.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

int main() {
    TempDirectory directory;
    KVStore store(directory.path());
    constexpr int writers = 6;
    constexpr int keys_per_writer = 120;
    std::atomic<bool> writing{true};
    std::atomic<bool> failed{false};

    std::thread reader([&] {
        try {
            while (writing.load()) {
                (void)store.listKeys();
                const auto value = store.get("writer-0-key-0");
                if (value && *value != "value-0") failed = true;
            }
        } catch (...) {
            failed = true;
        }
    });

    std::vector<std::thread> workers;
    for (int writer = 0; writer < writers; ++writer) {
        workers.emplace_back([&, writer] {
            try {
                for (int key = 0; key < keys_per_writer; ++key) {
                    const std::string name = "writer-" + std::to_string(writer) + "-key-" +
                                             std::to_string(key);
                    const std::string value = "value-" + std::to_string(writer);
                    store.put(name, value);
                    const auto found = store.get(name);
                    if (!found || *found != value) failed = true;
                }
            } catch (...) {
                failed = true;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    writing = false;
    reader.join();

    check(!failed.load(), "concurrent reads and writes should complete without inconsistent values");
    check(store.listKeys().size() == static_cast<size_t>(writers * keys_per_writer),
          "all concurrent writes should be present in the index");
    store.sync();
    store.close();

    KVStore recovered(directory.path());
    check(recovered.listKeys().size() == static_cast<size_t>(writers * keys_per_writer),
          "all concurrent writes should survive recovery");
    for (int writer = 0; writer < writers; ++writer) {
        const auto value = recovered.get("writer-" + std::to_string(writer) + "-key-0");
        check(value == std::optional<std::string>("value-" + std::to_string(writer)),
              "concurrent values should recover correctly");
    }
}
