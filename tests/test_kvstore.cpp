#include "kvstore.h"
#include "test_support.h"

#include <algorithm>
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
}
