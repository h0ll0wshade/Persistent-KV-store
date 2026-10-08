#include "logfile.h"
#include "test_support.h"

#include <fcntl.h>
#include <type_traits>
#include <unistd.h>

static_assert(!std::is_copy_constructible<LogFile>::value, "LogFile must not be copyable");
static_assert(std::is_move_constructible<LogFile>::value, "LogFile should be movable");

int main() {
    TempDirectory directory;
    const std::string path = directory.path() + "/data.log";
    Entry first{};
    {
        LogFile file(path, 12, true);
        Record record{};
        record.header.type = static_cast<uint8_t>(RecordType::Put);
        record.key = "key";
        record.value = "a value with spaces";
        first = file.append(record);
        check(file.read(first) == record.value, "value read should use the Entry offset");
        file.sync();
    }
    {
        LogFile reopened(path, 12, true);
        check(reopened.size() > first.value_pos, "reopened log should discover its existing size");
        LogFile moved(std::move(reopened));
        check(moved.read(first) == "a value with spaces", "move should transfer descriptor ownership");
        check(reopened.size() == 0, "moved-from log should have reset metadata");
        bool moved_from_rejected = false;
        try { (void)reopened.readRecord(0); }
        catch (const std::runtime_error&) { moved_from_rejected = true; }
        check(moved_from_rejected, "moved-from log should reject file operations");
        Record record{};
        record.header.type = static_cast<uint8_t>(RecordType::Put);
        record.key = "next";
        const Entry second = moved.append(record);
        check(second.value_size == 0, "empty value should be supported");
        check(moved.read(second).empty(), "empty value should read back");
        bool wrong_file_rejected = false;
        try { moved.read(Entry{99, first.value_pos, first.value_size}); }
        catch (const std::invalid_argument&) { wrong_file_rejected = true; }
        check(wrong_file_rejected, "entry from another file should be rejected");
    }

    const std::string truncated_path = directory.path() + "/truncated.log";
    {
        const char partial[] = "xx";
        const int fd = ::open(truncated_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        check(fd >= 0, "test should open log for truncation setup");
        check(::write(fd, partial, sizeof(partial)) == sizeof(partial), "test should append partial header");
        ::close(fd);
        bool rejected = false;
        try {
            LogFile file(truncated_path, 13, true);
            (void)file.readRecord(0);
        }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "truncated record header should be reported");
    }

    bool invalid_type_rejected = false;
    try {
        LogFile file(directory.path() + "/invalid.log", 14, true);
        Record invalid{};
        invalid.header.type = 99;
        (void)file.append(invalid);
    } catch (const std::invalid_argument&) { invalid_type_rejected = true; }
    check(invalid_type_rejected, "invalid record types should be rejected");

    bool invalid_delete_rejected = false;
    try {
        LogFile file(directory.path() + "/invalid-delete.log", 15, true);
        Record invalid{};
        invalid.header.type = static_cast<uint8_t>(RecordType::Delete);
        invalid.key = "key";
        invalid.value = "unexpected";
        (void)file.append(invalid);
    } catch (const std::invalid_argument&) { invalid_delete_rejected = true; }
    check(invalid_delete_rejected, "delete records must not carry a value");
}
