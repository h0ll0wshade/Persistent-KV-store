#pragma once

#include "logfile.h"
#include "options.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Owns the discovered log files and routes operations by file ID.
class LogManager {
public:
    explicit LogManager(const std::string& directory, const Options& options = {});

    LogFile& activeLog();
    LogFile& file(uint32_t file_id) const;
    Entry appendToActive(const Record& record);
    std::string read(const Entry& entry) const;
    std::vector<LogFile*> filesInOrder() const;
    void syncAll();

    void rotate();
    uint32_t highestFileId() const;
    uint64_t maxFileSize() const;
    const std::string& directoryPath() const;
    void createActiveLog(uint32_t file_id);
    void addImmutableLog(uint32_t file_id, const std::string& path);
    void removeLog(uint32_t file_id);

private:
    std::map<uint32_t, std::unique_ptr<LogFile>> files;
    uint32_t active_file_id;
    std::string directory;
    uint64_t max_file_size;

    void rotateLogFile();
};
