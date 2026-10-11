#include "kvstore.h"
#include "test_support.h"

#include <filesystem>
#include <map>

int main() {
    TempDirectory directory;
    KVStore store(directory.path());
    store.put("alpha", "old");
    store.put("alpha", "latest");
    store.put("beta", "deleted");
    store.erase("beta");

    const auto before = store.stats();
    check(before.file_count == 1, "stats should count numbered log files only");
    check(before.total_bytes == std::filesystem::file_size(
              std::filesystem::path(directory.path()) / "000001.log"),
          "stats total bytes should match physical log bytes");
    check(before.live_key_count == 1, "stats should report live keys");
    check(before.reclaimable_bytes > 0, "overwritten and deleted records should be reclaimable");

    std::map<std::string, std::string> folded;
    store.fold([&](const std::string& key, const std::string& value) {
        folded[key] = value;
        check(store.get(key) == std::optional<std::string>(value),
              "fold callback should run without holding the store read lock");
    });
    check(folded.size() == 1 && folded.at("alpha") == "latest",
          "fold should visit each live key and latest value exactly once");

    bool empty_callback_rejected = false;
    try { store.fold({}); }
    catch (const std::invalid_argument&) { empty_callback_rejected = true; }
    check(empty_callback_rejected, "fold should reject an empty callback");

    store.compact();
    const auto after = store.stats();
    check(after.live_key_count == 1, "compaction should leave live-key count unchanged");
    check(after.reclaimable_bytes == 0, "compaction should remove reclaimable records");
    check(store.get("alpha") == std::optional<std::string>("latest"),
          "compaction should preserve folded values");
}
