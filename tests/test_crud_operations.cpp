#include "crud_operations.h"
#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

int main() {
    TempDirectory directory;
    KeyDir keydir;
    LogFile log(directory.path() + "/crud.log", 4, true);
    CrudOperations crud(keydir, log);

    check(!crud.get("missing"), "missing key should return no value");
    crud.put("fruit", "apple");
    check(crud.get("fruit") == std::optional<std::string>("apple"), "put/get should round-trip");
    crud.put("fruit", "pear");
    check(crud.get("fruit") == std::optional<std::string>("pear"), "put should update the indexed location");
    crud.put("empty", "");
    check(crud.get("empty") == std::optional<std::string>(""), "empty value should be stored distinctly");

    auto keys = crud.listKeys();
    check(keys.size() == 2, "listKeys should expose the current index");
    check(std::find(keys.begin(), keys.end(), "fruit") != keys.end(), "listKeys should contain inserted keys");
    crud.erase("fruit");
    check(!crud.get("fruit"), "erase should remove a key from the index");
    crud.erase("missing");
    check(crud.listKeys().size() == 1, "erasing a missing key should preserve other keys");
    crud.sync();

    KeyDir unchanged_index;
    LogFile failed_log(directory.path() + "/failed.log", 5, true);
    LogFile moved_log(std::move(failed_log));
    CrudOperations failing_crud(unchanged_index, failed_log);
    bool append_failed = false;
    try { failing_crud.put("not-written", "value"); }
    catch (const std::runtime_error&) { append_failed = true; }
    check(append_failed, "CRUD should propagate a failed append");
    check(!unchanged_index.get("not-written"), "failed append must not update the index");
}
