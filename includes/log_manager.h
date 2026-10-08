#pragma once

#include "logfile.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Owns the discovered log files and routes operations by file ID.
class LogManager {
public:
    explicit LogManager(const std::string& directory);

    LogFile& activeLog();
    LogFile& file(uint32_t file_id) const;
    Entry appendToActive(const Record& record);
    std::string read(const Entry& entry) const;
    std::vector<LogFile*> filesInOrder() const;
    void syncAll();

private:
    std::map<uint32_t, std::unique_ptr<LogFile>> files;
    uint32_t active_file_id;
};
