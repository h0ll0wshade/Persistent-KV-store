#include "kvstore.h"
#include "logfile.h"
#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {
Record makeRecord(RecordType type, const std::string& key, const std::string& value = {}) {
    Record record{};
    record.header.type = static_cast<uint8_t>(type);
    record.key = key;
    record.value = value;
    return record;
}

void appendJunk(const std::string& path, char byte) {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    check(output.good(), "junk setup should open the log");
    output.write(&byte, 1);
    check(output.good(), "junk setup should write a byte");
}
}

int main() {
    TempDirectory directory;
    const auto first_path = (std::filesystem::path(directory.path()) / "000001.log").string();
    const auto later_path = (std::filesystem::path(directory.path()) / "000004.log").string();

    {
        LogFile first(first_path, 1, false);
        first.append(makeRecord(RecordType::Put, "alpha", "old"));
        first.append(makeRecord(RecordType::Put, "removed", "gone"));
        first.append(makeRecord(RecordType::Delete, "removed"));
        first.append(makeRecord(RecordType::Put, "shared", "older"));
    }
    {
        LogFile later(later_path, 4, true);
        later.append(makeRecord(RecordType::Put, "alpha", "latest"));
        later.append(makeRecord(RecordType::Put, "shared", "newer"));
    }
    appendJunk(later_path, static_cast<char>(0xff));
    {
        LogFile later(later_path, 4, true);
        later.append(makeRecord(RecordType::Put, "after-junk", "found"));
    }
    {
        std::ofstream unrelated(std::filesystem::path(directory.path()) / "notes.log");
        unrelated << "not a numbered log";
    }

    {
        KVStore recovered(directory.path());
        check(recovered.get("alpha") == std::optional<std::string>("latest"),
              "later log record should replace earlier value");
        check(recovered.get("shared") == std::optional<std::string>("newer"),
              "recovery should use the latest record across files");
        check(!recovered.get("removed"), "tombstone should remain deleted after recovery");
        check(recovered.get("after-junk") == std::optional<std::string>("found"),
              "recovery should resume after malformed bytes");
        const auto keys = recovered.listKeys();
        check(keys.size() == 3, "recovery should build the complete live-key index");

        recovered.put("written-after-restart", "persisted");
        recovered.erase("shared");
        recovered.sync();
    }

    {
        KVStore recovered_again(directory.path());
        check(recovered_again.get("written-after-restart") == std::optional<std::string>("persisted"),
              "new append should be discovered on a later restart");
        check(!recovered_again.get("shared"), "new tombstone should be replayed on restart");
        check(recovered_again.get("alpha") == std::optional<std::string>("latest"),
              "older log values should remain available after restart");
    }

    TempDirectory empty_directory;
    {
        KVStore empty(empty_directory.path());
        check(!empty.get("anything"), "empty directory should recover to an empty index");
        check(std::filesystem::exists(std::filesystem::path(empty_directory.path()) / "000001.log"),
              "empty database should create its initial active log");
    }

    TempDirectory duplicate_directory;
    {
        std::ofstream first(std::filesystem::path(duplicate_directory.path()) / "000001.log");
        std::ofstream duplicate(std::filesystem::path(duplicate_directory.path()) / "0000001.log");
    }
    bool duplicate_id_rejected = false;
    try { KVStore invalid(duplicate_directory.path()); }
    catch (const std::runtime_error&) { duplicate_id_rejected = true; }
    check(duplicate_id_rejected, "duplicate numeric file IDs should fail open");

    TempDirectory zero_id_directory;
    {
        std::ofstream zero(std::filesystem::path(zero_id_directory.path()) / "000000.log");
    }
    bool zero_id_rejected = false;
    try { KVStore invalid(zero_id_directory.path()); }
    catch (const std::runtime_error&) { zero_id_rejected = true; }
    check(zero_id_rejected, "file ID zero should fail open");

    TempDirectory oversized_id_directory;
    {
        std::ofstream oversized(std::filesystem::path(oversized_id_directory.path()) / "4294967296.log");
    }
    bool oversized_id_rejected = false;
    try { KVStore invalid(oversized_id_directory.path()); }
    catch (const std::runtime_error&) { oversized_id_rejected = true; }
    check(oversized_id_rejected, "file IDs outside uint32 range should fail open");
}
