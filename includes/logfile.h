#pragma once

#include "entry.h"
#include "records.h"

#include <cstdint>
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

    Entry append(const Record& record);
    Record read(const Entry& entry) const;
};