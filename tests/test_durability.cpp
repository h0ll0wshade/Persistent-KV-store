#include "kvstore.h"
#include "records.h"
#include "record_codec.h"
#include "test_support.h"

#include <optional>

int main() {
    static_assert(Options{}.sync_on_put == false,
                  "writes should be buffered by default");

    TempDirectory buffered_directory;
    const std::string nested_database = buffered_directory.path() + "/new/levels/database";
    {
        KVStore store(nested_database);
        store.put("buffered", "value");
        store.sync();
    }
    {
        KVStore recovered(nested_database);
        check(recovered.get("buffered") == std::optional<std::string>("value"),
              "explicit sync should leave the value recoverable");
    }

    TempDirectory sync_on_put_directory;
    Options options;
    options.max_file_size = RecordCodec::kHeaderSize + 2;
    options.sync_on_put = true;
    {
        KVStore store(sync_on_put_directory.path(), options);
        store.put("a", "1");
        store.put("b", "2"); // Forces a rotation before the second sync-on-put.
        store.erase("a");
        check(!store.get("a"), "sync-on-put mode should apply to tombstone writes too");
        store.put("a", "3");
    }
    {
        KVStore recovered(sync_on_put_directory.path(), options);
        check(recovered.get("a") == std::optional<std::string>("3"),
              "sync-on-put writes should recover after reopening");
        check(recovered.get("b") == std::optional<std::string>("2"),
              "sync-on-put should continue to work across rotation");
        recovered.sync();
    }
}
