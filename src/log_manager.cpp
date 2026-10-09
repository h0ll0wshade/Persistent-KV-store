#include "log_manager.h"

#include "directory_sync.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace {
struct DiscoveredLog {
    uint32_t file_id;
    std::filesystem::path path;
};

bool isDecimal(const std::string& value) {
    if (value.size() < 6) return false;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

std::vector<DiscoveredLog> discoverLogs(const std::filesystem::path& directory) {
    std::vector<DiscoveredLog> discovered;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file()) continue;
        const auto filename = item.path().filename().string();
        if (item.path().extension() != ".log") continue;
        const auto stem = item.path().stem().string();
        if (!isDecimal(stem)) continue;

        uint64_t parsed = 0;
        const auto result = std::from_chars(stem.data(), stem.data() + stem.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != stem.data() + stem.size() ||
            parsed == 0 || parsed > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("Invalid log file ID in filename: " + filename);
        }
        discovered.push_back(DiscoveredLog{static_cast<uint32_t>(parsed), item.path()});
    }

    std::sort(discovered.begin(), discovered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.file_id < rhs.file_id;
    });
    for (size_t i = 1; i < discovered.size(); ++i) {
        if (discovered[i - 1].file_id == discovered[i].file_id) {
            throw std::runtime_error("Multiple log filenames resolve to the same file ID");
        }
    }
    return discovered;
}

std::filesystem::path logPath(const std::filesystem::path& directory, uint32_t file_id) {
    std::ostringstream filename;
    filename << std::setw(6) << std::setfill('0') << file_id << ".log";
    return directory / filename.str();
}

void createDirectoriesDurably(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> missing;
    auto current = directory;
    std::error_code error;
    while (!current.empty() && !std::filesystem::exists(current, error)) {
        if (error) throw std::runtime_error("Failed to inspect database directory: " + error.message());
        missing.push_back(current);
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    if (error) throw std::runtime_error("Failed to inspect database directory: " + error.message());

    std::filesystem::create_directories(directory, error);
    if (error) throw std::runtime_error("Failed to create database directory: " + error.message());

    // Each child entry lives in its parent directory. Sync from the closest
    // existing ancestor down through the newly created path.
    for (const auto& created : missing) {
        auto parent = created.parent_path();
        if (parent.empty()) parent = ".";
        syncDirectory(parent.string());
    }
}
}

LogManager::LogManager(const std::string& directory_path, const Options& options)
    : active_file_id(0), directory(directory_path), max_file_size(options.max_file_size) {
    if (max_file_size == 0) throw std::invalid_argument("max_file_size must be greater than zero");
    const std::filesystem::path root(directory_path);
    createDirectoriesDurably(root);

    auto discovered = discoverLogs(root);
    const bool creating_initial_log = discovered.empty();
    if (creating_initial_log) discovered.push_back(DiscoveredLog{1, logPath(root, 1)});

    active_file_id = discovered.back().file_id;
    for (const auto& log : discovered) {
        files.emplace(log.file_id,
                      std::make_unique<LogFile>(log.path.string(), log.file_id,
                                                log.file_id == active_file_id));
    }
    if (creating_initial_log) syncDirectory(directory);
}

LogFile& LogManager::activeLog() {
    return *files.at(active_file_id);
}

LogFile& LogManager::file(uint32_t file_id) const {
    const auto found = files.find(file_id);
    if (found == files.end()) throw std::runtime_error("No log file for file ID " + std::to_string(file_id));
    return *found->second;
}

Entry LogManager::appendToActive(const Record& record) {
    const uint64_t record_size = sizeof(RecordHeader) +
                                 static_cast<uint64_t>(record.key.size()) +
                                 static_cast<uint64_t>(record.value.size());
    const uint64_t current_size = activeLog().size();
    if (current_size != 0 &&
        (current_size >= max_file_size || record_size > max_file_size - current_size)) {
        rotateLogFile();
    }
    return activeLog().append(record);
}

void LogManager::rotateLogFile() {
    if (active_file_id == std::numeric_limits<uint32_t>::max())
        throw std::overflow_error("Log file ID space exhausted");

    createActiveLog(active_file_id + 1);
}

void LogManager::rotate() {
    rotateLogFile();
}

uint32_t LogManager::highestFileId() const {
    return files.empty() ? 0 : files.rbegin()->first;
}

uint64_t LogManager::maxFileSize() const {
    return max_file_size;
}

const std::string& LogManager::directoryPath() const {
    return directory;
}

void LogManager::createActiveLog(uint32_t file_id) {
    if (file_id <= highestFileId())
        throw std::invalid_argument("New active log ID must be greater than existing IDs");
    const auto path = logPath(directory, file_id);
    auto next = std::make_unique<LogFile>(path.string(), file_id, true);
    syncDirectory(directory);
    files.emplace(file_id, std::move(next));
    active_file_id = file_id;
}

void LogManager::addImmutableLog(uint32_t file_id, const std::string& path) {
    if (files.find(file_id) != files.end())
        throw std::invalid_argument("Log file ID is already registered");
    auto log = std::make_unique<LogFile>(path, file_id, false);
    files.emplace(file_id, std::move(log));
}

void LogManager::removeLog(uint32_t file_id) {
    if (file_id == active_file_id)
        throw std::invalid_argument("Cannot remove the active log file");
    const auto found = files.find(file_id);
    if (found == files.end()) return;

    const auto path = logPath(directory, file_id);
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error) throw std::runtime_error("Failed to remove log file: " + error.message());
    if (!removed) throw std::runtime_error("Expected log file was missing during removal");
    files.erase(found);
    syncDirectory(directory);
}

std::string LogManager::read(const Entry& entry) const {
    return file(entry.file_id).read(entry);
}

std::vector<LogFile*> LogManager::filesInOrder() const {
    std::vector<LogFile*> ordered;
    ordered.reserve(files.size());
    for (const auto& item : files) ordered.push_back(item.second.get());
    return ordered;
}

void LogManager::syncAll() {
    for (auto& item : files) item.second->sync();
    syncDirectory(directory);
}
