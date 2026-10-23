// Exercises the public API, close behavior, read-only access, and process locks.
#include "kvstore.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_copy_constructible<KVStore>::value, "KVStore must not be copyable");
static_assert(!std::is_move_constructible<KVStore>::value, "KVStore is intentionally not movable yet");

int main() {
    TempDirectory directory;
    KVStore store(directory.path());
    check(!store.get("missing"), "missing key should return nullopt");
    store.put("fruit", "apple");
    check(store.get("fruit") == std::optional<std::string>("apple"), "put/get should round-trip");
    store.put("fruit", "pear");
    check(store.get("fruit") == std::optional<std::string>("pear"), "put should update a key");
    store.put("empty", "");
    check(store.get("empty") == std::optional<std::string>(""), "empty value should differ from missing key");
    auto keys = store.listKeys();
    check(keys.size() == 2, "listKeys should return current live keys");
    check(std::find(keys.begin(), keys.end(), "fruit") != keys.end(), "listKeys should include inserted key");
    store.erase("fruit");
    check(!store.get("fruit"), "erase should remove a key from the index");
    store.erase("absent");
    check(store.listKeys().size() == 1, "deleting absent key should leave other keys intact");
    store.sync();
    store.close();
    store.close();
    bool closed_rejected = false;
    try { (void)store.get("fruit"); }
    catch (const std::runtime_error&) { closed_rejected = true; }
    check(closed_rejected, "operations after close should fail clearly");

    TempDirectory readonly_directory;
    {
        KVStore writer(readonly_directory.path());
        writer.put("read", "only");
        writer.sync();
        writer.close();
    }
    std::vector<std::string> before;
    for (const auto& item : std::filesystem::directory_iterator(readonly_directory.path()))
        before.push_back(item.path().filename().string());
    std::sort(before.begin(), before.end());
    Options readonly_options;
    readonly_options.read_only = true;
    {
        KVStore reader(readonly_directory.path(), readonly_options);
        check(reader.get("read") == std::optional<std::string>("only"),
              "read-only open should recover and serve existing values");
        bool write_rejected = false;
        try { reader.put("new", "value"); }
        catch (const std::runtime_error&) { write_rejected = true; }
        check(write_rejected, "read-only writes should fail");
        bool erase_rejected = false;
        try { reader.erase("read"); }
        catch (const std::runtime_error&) { erase_rejected = true; }
        check(erase_rejected, "read-only deletes should fail");
        reader.close();
    }
    std::vector<std::string> after;
    for (const auto& item : std::filesystem::directory_iterator(readonly_directory.path()))
        after.push_back(item.path().filename().string());
    std::sort(after.begin(), after.end());
    check(before == after, "read-only access should not create or remove database files");

    TempDirectory lock_directory;
    KVStore exclusive(lock_directory.path());
    bool second_writer_rejected = false;
    try { KVStore competing(lock_directory.path()); }
    catch (const std::runtime_error&) { second_writer_rejected = true; }
    check(second_writer_rejected, "a second writer process handle should be rejected");
    exclusive.close();
}
