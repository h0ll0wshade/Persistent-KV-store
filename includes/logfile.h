#pragma once

#include "entry.h"
#include "records.h"

#include <cstdint>
#include <optional>
#include <string>

struct FileMetadata {
    uint32_t file_id;
    uint64_t size;
    bool active;
};

class LogFile {
private:
    FileMetadata metadata;
    int fd;
    uint64_t write_offset;

public:
    LogFile(const std::string& path, uint32_t file_id, bool active);
    ~LogFile();

    //deleted copy semantics
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    LogFile(LogFile&& other) noexcept;
    LogFile& operator=(LogFile&& other) noexcept;

    Entry append(const Record& record);
    std::string read(const Entry& entry) const;
    std::optional<Record> readRecord(uint64_t offset) const;
    uint64_t size() const;
    uint32_t fileId() const;
    void sync();
};
