#include "kvstore.h"
#include "records.h"
#include "test_support.h"

#include <filesystem>
#include <optional>

int main() {
    TempDirectory directory;
    Options options;
    const uint64_t one_record = sizeof(RecordHeader) + 1 + 1;
    options.max_file_size = one_record * 2;

    {
        KVStore store(directory.path(), options);
        store.put("a", "1");
        store.put("b", "2"); // Exactly reaches the limit; no rotation yet.
        check(std::filesystem::exists(std::filesystem::path(directory.path()) / "000001.log"),
              "first log should exist");
        check(!std::filesystem::exists(std::filesystem::path(directory.path()) / "000002.log"),
              "an append ending exactly at the limit should not rotate");

        store.put("c", "3");
        check(std::filesystem::exists(std::filesystem::path(directory.path()) / "000002.log"),
              "append that would exceed the limit should rotate first");
        check(store.get("a") == std::optional<std::string>("1"),
              "values in immutable files should remain readable");
        check(store.get("c") == std::optional<std::string>("3"),
              "new values should be written to the active file");
    }

    {
        KVStore recovered(directory.path(), options);
        check(recovered.get("a") == std::optional<std::string>("1"),
              "recovery should replay the first rotated file");
        check(recovered.get("b") == std::optional<std::string>("2"),
              "recovery should replay records at the rotation boundary");
        check(recovered.get("c") == std::optional<std::string>("3"),
              "recovery should replay the newest active file");
        recovered.put("d", "4");
        check(!std::filesystem::exists(std::filesystem::path(directory.path()) / "000003.log"),
              "reopened active file should still accept a record at the exact limit");
        recovered.put("e", "5");
        check(std::filesystem::exists(std::filesystem::path(directory.path()) / "000003.log"),
              "reopened store should continue with the next file ID");
    }

    TempDirectory oversized_directory;
    Options tiny;
    tiny.max_file_size = 1;
    {
        KVStore store(oversized_directory.path(), tiny);
        store.put("large-key", "larger-value");
        check(store.get("large-key") == std::optional<std::string>("larger-value"),
              "an oversized record should be accepted into an empty log");
        store.put("next", "value");
        check(std::filesystem::exists(std::filesystem::path(oversized_directory.path()) / "000002.log"),
              "the append after an oversized record should rotate");
    }

    bool zero_limit_rejected = false;
    try {
        Options invalid;
        invalid.max_file_size = 0;
        KVStore store(oversized_directory.path() + "/invalid", invalid);
    } catch (const std::invalid_argument&) {
        zero_limit_rejected = true;
    }
    check(zero_limit_rejected, "zero max_file_size should be rejected");
}
