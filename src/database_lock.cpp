#include "database_lock.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "directory_sync.h"

DatabaseLock::DatabaseLock(const std::string& directory, bool read_only) {
    if (!read_only) {
        std::error_code error;
        const bool existed = std::filesystem::exists(directory, error);
        if (error) throw std::runtime_error("Failed to inspect database directory: " + error.message());
        std::filesystem::create_directories(directory, error);
        if (error) throw std::runtime_error("Failed to create database directory: " + error.message());
        if (!existed) {
            auto parent = std::filesystem::path(directory).parent_path();
            if (parent.empty()) parent = ".";
            syncDirectory(parent.string());
        }
    }
    const std::string path = directory + "/.lock";
    fd = ::open(path.c_str(), read_only ? O_RDONLY : (O_RDWR | O_CREAT), 0644);
    if (fd == -1) {
        throw std::runtime_error("Failed to open database lock: " + std::string(std::strerror(errno)));
    }
    const int operation = (read_only ? LOCK_SH : LOCK_EX) | LOCK_NB;
    if (::flock(fd, operation) == -1) {
        const int saved_errno = errno;
        ::close(fd);
        fd = -1;
        throw std::runtime_error("Database is already locked: " + std::string(std::strerror(saved_errno)));
    }
    if (!read_only) syncDirectory(directory);
}

DatabaseLock::~DatabaseLock() {
    if (fd != -1) {
        ::flock(fd, LOCK_UN);
        ::close(fd);
    }
}
