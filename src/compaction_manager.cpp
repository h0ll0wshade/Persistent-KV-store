#include "compaction_manager.h"

#include "directory_sync.h"
#include "kvdir.h"
#include "log_manager.h"
#include "logfile.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace {
struct StagedLog {
    uint32_t file_id;
    std::filesystem::path temporary_path;
    std::unique_ptr<LogFile> file;
};

class TemporaryFiles {
public:
    ~TemporaryFiles() {
        for (const auto& path : paths) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }

    void add(const std::filesystem::path& path) { paths.push_back(path); }

private:
    std::vector<std::filesystem::path> paths;
};

std::filesystem::path numberedLogPath(const std::string& directory, uint32_t file_id) {
    std::ostringstream name;
    name << std::setw(6) << std::setfill('0') << file_id << ".log";
    return std::filesystem::path(directory) / name.str();
}

std::filesystem::path uniqueTemporaryPath(const std::string& directory) {
    std::string pattern = (std::filesystem::path(directory) / ".compact-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const int fd = ::mkstemp(writable.data());
    if (fd == -1) throw std::runtime_error("Failed to create compaction temporary file");
    if (::close(fd) == -1) {
        const int saved_errno = errno;
        std::error_code ignored;
        std::filesystem::remove(writable.data(), ignored);
        throw std::runtime_error("Failed to close compaction temporary file: " +
                                 std::string(std::strerror(saved_errno)));
    }
    return std::filesystem::path(writable.data());
}

bool isCurrentLiveRecord(const LogFile& log, uint64_t offset,
                         const Record& record, const KeyDir& keydir) {
    if (record.header.type != static_cast<uint8_t>(RecordType::Put)) return false;
    const auto current = keydir.get(record.key);
    if (!current) return false;
    const uint64_t value_pos = offset + sizeof(RecordHeader) + record.header.key_size;
    return current->file_id == log.fileId() && current->value_pos == value_pos &&
           current->value_size == record.header.value_size;
}

template <typename Callback>
void visitCurrentRecords(const std::vector<LogFile*>& source_logs,
                         const KeyDir& keydir, Callback callback) {
    for (const LogFile* log : source_logs) {
        uint64_t offset = 0;
        while (offset < log->size()) {
            const auto record = log->readRecord(offset);
            if (!record) break;
            const uint64_t record_size = sizeof(RecordHeader) + record->header.key_size +
                                         static_cast<uint64_t>(record->header.value_size);
            if (isCurrentLiveRecord(*log, offset, *record, keydir))
                callback(*record, record_size);
            offset += record_size;
        }
    }
}

std::vector<LogFile*> immutableLogs(LogManager& logs) {
    std::vector<LogFile*> result;
    for (LogFile* log : logs.filesInOrder()) {
        if (log->fileId() != logs.activeLog().fileId()) result.push_back(log);
    }
    return result;
}
}

CompactionManager::CompactionManager(KeyDir& index, LogManager& log_manager)
    : keydir(index), logs(log_manager) {}

void CompactionManager::compact() {
    // Seal the current writer so every source record is immutable for both passes.
    logs.rotate();
    const auto sources = immutableLogs(logs);

    uint64_t count_output_files = 0;
    uint64_t output_size = 0;
    visitCurrentRecords(sources, keydir, [&](const Record&, uint64_t record_size) {
        if (count_output_files == 0) {
            count_output_files = 1;
        } else if (output_size != 0 &&
                   (output_size >= logs.maxFileSize() ||
                    record_size > logs.maxFileSize() - output_size)) {
            ++count_output_files;
            output_size = 0;
        }
        output_size += record_size;
    });

    const uint64_t first_output_id = static_cast<uint64_t>(logs.highestFileId()) + 1;
    if (count_output_files > 0 &&
        first_output_id + count_output_files > std::numeric_limits<uint32_t>::max()) {
        throw std::overflow_error("Log file ID space exhausted during compaction");
    }

    TemporaryFiles temporary_files;
    std::vector<StagedLog> staged;
    staged.reserve(static_cast<size_t>(count_output_files));
    for (uint64_t index = 0; index < count_output_files; ++index) {
        const uint32_t file_id = static_cast<uint32_t>(first_output_id + index);
        const auto path = uniqueTemporaryPath(logs.directoryPath());
        temporary_files.add(path);
        staged.push_back(StagedLog{file_id, path,
                                   std::make_unique<LogFile>(path.string(), file_id, false)});
    }

    size_t output_index = 0;
    output_size = 0;
    visitCurrentRecords(sources, keydir, [&](const Record& record, uint64_t record_size) {
        if (output_size != 0 &&
            (output_size >= logs.maxFileSize() ||
             record_size > logs.maxFileSize() - output_size)) {
            ++output_index;
            output_size = 0;
        }
        staged.at(output_index).file->append(record);
        output_size += record_size;
    });
    for (auto& output : staged) output.file->sync();

    // The new active ID must sort after every compacted output on recovery.
    if (!staged.empty()) {
        const uint64_t active_id = first_output_id + count_output_files;
        if (active_id > std::numeric_limits<uint32_t>::max())
            throw std::overflow_error("Log file ID space exhausted during compaction");
        logs.createActiveLog(static_cast<uint32_t>(active_id));
    }

    std::vector<std::filesystem::path> published_paths;
    published_paths.reserve(staged.size());
    for (auto& output : staged) {
        const auto final_path = numberedLogPath(logs.directoryPath(), output.file_id);
        std::error_code error;
        std::filesystem::rename(output.temporary_path, final_path, error);
        if (error) throw std::runtime_error("Failed to publish compacted log: " + error.message());
        published_paths.push_back(final_path);
    }
    if (!published_paths.empty()) syncDirectory(logs.directoryPath());

    for (size_t index = 0; index < staged.size(); ++index) {
        staged[index].file.reset();
        logs.addImmutableLog(staged[index].file_id, published_paths[index].string());
    }

    // Rebuild output locations from the durable, published records before deleting sources.
    std::vector<LogFile*> compacted_logs;
    compacted_logs.reserve(staged.size());
    for (const auto& output : staged) compacted_logs.push_back(&logs.file(output.file_id));
    for (const LogFile* log : compacted_logs) {
        uint64_t offset = 0;
        while (offset < log->size()) {
            const auto record = log->readRecord(offset);
            if (!record) break;
            const uint64_t value_pos = offset + sizeof(RecordHeader) + record->header.key_size;
            keydir.put(record->key,
                       Entry{log->fileId(), value_pos, record->header.value_size});
            offset += sizeof(RecordHeader) + record->header.key_size + record->header.value_size;
        }
    }

    std::vector<uint32_t> source_ids;
    for (LogFile* log : logs.filesInOrder()) {
        const bool compacted_output =
            std::any_of(staged.begin(), staged.end(), [&](const StagedLog& output) {
                return output.file_id == log->fileId();
            });
        if (!compacted_output && log->fileId() != logs.activeLog().fileId())
            source_ids.push_back(log->fileId());
    }
    for (const uint32_t file_id : source_ids) logs.removeLog(file_id);
}
