#include "kvstore.h"
#include "records.h"
#include "test_support.h"

#include <filesystem>
#include <optional>

namespace {
size_t numberedLogs(const std::string& directory) {
    size_t count = 0;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (item.is_regular_file() && item.path().extension() == ".log" &&
            item.path().stem().string().size() >= 6) {
            ++count;
        }
    }
    return count;
}
}

int main() {
    TempDirectory directory;
    Options options;
    options.max_file_size = sizeof(RecordHeader) + 2;

    size_t files_before = 0;
    {
        KVStore store(directory.path(), options);
        store.put("a", "1");
        store.put("b", "1");
        store.put("a", "2");
        store.put("c", "3");
        store.erase("b");
        store.put("d", "4");
        store.put("e", "5");
        files_before = numberedLogs(directory.path());
        check(files_before >= 3, "test setup should create multiple immutable logs");

        store.compact();
        check(store.get("a") == std::optional<std::string>("2"),
              "compaction should preserve the latest overwritten value");
        check(store.get("b") == std::nullopt,
              "compaction should preserve tombstone semantics");
        check(store.get("c") == std::optional<std::string>("3"),
              "compaction should preserve values from older files");
        check(store.get("d") == std::optional<std::string>("4"),
              "compaction should preserve values from newer files");
        check(store.get("e") == std::optional<std::string>("5"),
              "compaction should preserve every live key");
        check(numberedLogs(directory.path()) < files_before,
              "compaction should remove obsolete source log files");
    }

    {
        KVStore recovered(directory.path(), options);
        check(recovered.get("a") == std::optional<std::string>("2"),
              "restart after compaction should rebuild the latest value");
        check(!recovered.get("b"),
              "restart after compaction must not resurrect a deleted key");
        check(recovered.get("c") == std::optional<std::string>("3") &&
                  recovered.get("d") == std::optional<std::string>("4") &&
                  recovered.get("e") == std::optional<std::string>("5"),
              "restart after compaction should restore all remaining live values");

        recovered.erase("a");
        recovered.erase("c");
        recovered.erase("d");
        recovered.erase("e");
        recovered.compact();
        check(recovered.listKeys().empty(), "compaction should support a database with no live keys");
    }

    {
        KVStore empty_after_compaction(directory.path(), options);
        check(empty_after_compaction.listKeys().empty(),
              "restart after compacting all tombstoned keys should remain empty");
    }

    TempDirectory empty_directory;
    {
        KVStore empty(empty_directory.path(), options);
        empty.compact();
        check(empty.listKeys().empty(), "compacting an empty database should be safe");
    }
    {
        KVStore reopened(empty_directory.path(), options);
        check(reopened.listKeys().empty(), "empty compaction should remain recoverable");
    }
}
