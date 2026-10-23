// Tests restart replay, damaged-record recovery, and non-mutating scans.
#include "kvstore.h"
#include "logfile.h"
#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <fcntl.h>
#include <unistd.h>

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

void corruptByte(const std::string& path, uint64_t offset) {
    const int fd = ::open(path.c_str(), O_RDWR);
    check(fd >= 0, "test should open log for corruption setup");
    char byte = 'X';
    check(::pwrite(fd, &byte, 1, static_cast<off_t>(offset)) == 1,
          "test should corrupt one payload byte");
    ::close(fd);
}
}

int main() {
    TempDirectory directory;
    const auto first_path = (std::filesystem::path(directory.path()) / "000001.log").string();
    const auto later_path = (std::filesystem::path(directory.path()) / "000004.log").string();

    // Initialize the database format marker before building recovery fixtures.
    { KVStore initialized(directory.path()); }

    {
        LogFile first(first_path, 1, false);
        first.append(makeRecord(RecordType::Put, "alpha", "old"));
        first.append(makeRecord(RecordType::Put, "removed", "gone"));
        first.append(makeRecord(RecordType::Delete, "removed"));
        first.append(makeRecord(RecordType::Put, "shared", "older"));
    }
    Entry damaged_entry{};
    {
        LogFile later(later_path, 4, true);
        later.append(makeRecord(RecordType::Put, "alpha", "latest"));
        damaged_entry = later.append(makeRecord(RecordType::Put, "damaged", "bad-value"));
        later.append(makeRecord(RecordType::Put, "shared", "newer"));
    }
    corruptByte(later_path, damaged_entry.value_pos);
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
        check(!recovered.get("damaged"), "checksum-invalid records should be ignored");
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

    TempDirectory legacy_directory;
    const auto legacy_log = std::filesystem::path(legacy_directory.path()) / "000001.log";
    {
        std::ofstream legacy(legacy_log, std::ios::binary);
        legacy << "native legacy data";
    }
    bool legacy_rejected = false;
    try { KVStore unsupported(legacy_directory.path()); }
    catch (const std::runtime_error&) { legacy_rejected = true; }
    check(legacy_rejected, "populated databases without FORMAT marker should fail fast");
    check(std::filesystem::file_size(legacy_log) == std::string("native legacy data").size(),
          "rejecting a legacy database must not alter its data");

    TempDirectory truncated_directory;
    const auto truncated_log = std::filesystem::path(truncated_directory.path()) / "000001.log";
    {
        KVStore initialized(truncated_directory.path());
        initialized.put("safe", "kept");
        initialized.put("tail", "incomplete");
        initialized.sync();
    }
    const auto original_size = std::filesystem::file_size(truncated_log);
    std::filesystem::resize_file(truncated_log, original_size - 3);
    const auto damaged_size = std::filesystem::file_size(truncated_log);
    {
        KVStore recovered(truncated_directory.path());
        check(recovered.get("safe") == std::optional<std::string>("kept"),
              "recovery should retain records before an incomplete tail");
        check(!recovered.get("tail"), "incomplete trailing record should be ignored");
        check(std::filesystem::file_size(truncated_log) == damaged_size,
              "recovery should not truncate or rewrite an incomplete tail");
        recovered.put("after-tail", "found");
        recovered.sync();
    }
    {
        KVStore recovered(truncated_directory.path());
        check(recovered.get("after-tail") == std::optional<std::string>("found"),
              "recovery should resynchronize to valid records appended after an incomplete tail");
    }
}
