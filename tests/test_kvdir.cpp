#include "kvdir.h"
#include "test_support.h"

#include <algorithm>

int main() {
    KeyDir directory;
    check(!directory.get("missing"), "missing key should return no entry");
    directory.put("a", Entry{1, 10, 2});
    directory.put("b", Entry{2, 20, 3});
    check(directory.get("a")->file_id == 1, "lookup should return inserted entry");
    directory.put("a", Entry{3, 30, 4});
    check(directory.get("a")->file_id == 3, "put should replace the prior entry");
    auto keys = directory.listKeys();
    check(keys.size() == 2, "listKeys should return live keys");
    check(std::find(keys.begin(), keys.end(), "b") != keys.end(), "listKeys should include keys");
    directory.erase("a");
    check(!directory.get("a"), "erase should remove the key");
    check(directory.listKeys().size() == 1, "listKeys should reflect erase");
}
