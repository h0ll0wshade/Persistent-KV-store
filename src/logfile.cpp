#include "logfile.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::runtime_error ioError(const char* operation) {
    return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

void pwriteAll(int fd, const void* data, size_t count, uint64_t offset) {
    const auto* bytes = static_cast<const char*>(data);
    size_t done = 0;
    while (done < count) {
        const ssize_t result = ::pwrite(fd, bytes + done, count - done,
                                        static_cast<off_t>(offset + done));
        if (result < 0) {
            if (errno == EINTR) continue;
            throw ioError("pwrite failed");
        }
        if (result == 0) throw std::runtime_error("pwrite made no progress");
        done += static_cast<size_t>(result);
    }
}

void preadAll(int fd, void* data, size_t count, uint64_t offset) {
    auto* bytes = static_cast<char*>(data);
    size_t done = 0;
    while (done < count) {
        const ssize_t result = ::pread(fd, bytes + done, count - done,
                                      static_cast<off_t>(offset + done));
        if (result < 0) {
            if (errno == EINTR) continue;
            throw ioError("pread failed");
        }
        if (result == 0) throw std::runtime_error("Unexpected end of log file");
        done += static_cast<size_t>(result);
    }
}
}

LogFile::LogFile(const std::string& path, uint32_t file_id, bool active)
    : metadata{file_id, 0, active}, fd{-1}, write_offset{0} {
    fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd == -1) throw ioError("open failed");

    struct stat info {};
    if (::fstat(fd, &info) == -1) {
        const int saved_errno = errno;
        ::close(fd);
        fd = -1;
        errno = saved_errno;
        throw ioError("fstat failed");
    }
    if (info.st_size < 0) {
        ::close(fd);
        fd = -1;
        throw std::runtime_error("Log file has a negative size");
    }
    write_offset = static_cast<uint64_t>(info.st_size);
    metadata.size = write_offset;
}

LogFile::~LogFile() {
    if (fd != -1) ::close(fd);
}

LogFile::LogFile(LogFile&& other) noexcept
    : metadata(other.metadata), fd(other.fd), write_offset(other.write_offset) {
    other.fd = -1;
    other.metadata = FileMetadata{0, 0, false};
    other.write_offset = 0;
}

LogFile& LogFile::operator=(LogFile&& other) noexcept {
    if (this == &other) return *this;
    if (fd != -1) ::close(fd);
    metadata = other.metadata;
    fd = other.fd;
    write_offset = other.write_offset;
    other.fd = -1;
    other.metadata = FileMetadata{0, 0, false};
    other.write_offset = 0;
    return *this;
}

Entry LogFile::append(const Record& record) {
    if (fd == -1) throw std::runtime_error("Cannot append to a closed log file");
    if (record.key.size() > std::numeric_limits<uint32_t>::max() ||
        record.value.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("Record field exceeds the on-disk size limit");
    }
    if (record.header.type != static_cast<uint8_t>(RecordType::Put) &&
        record.header.type != static_cast<uint8_t>(RecordType::Delete)) {
        throw std::invalid_argument("Unknown record type");
    }

    RecordHeader header;
    std::memset(&header, 0, sizeof(header));
    header.key_size = static_cast<uint32_t>(record.key.size());
    header.value_size = static_cast<uint32_t>(record.value.size());
    header.type = record.header.type;
    if (record.header.type == static_cast<uint8_t>(RecordType::Delete) && !record.value.empty())
        throw std::invalid_argument("Delete record must not contain a value");
    const uint64_t start = write_offset;
    const uint64_t value_pos = start + sizeof(header) + record.key.size();

    // Keep the logical offset unchanged until every field has been written. A
    // retry after an error overwrites any partial bytes at this same position.
    pwriteAll(fd, &header, sizeof(header), start);
    pwriteAll(fd, record.key.data(), record.key.size(), start + sizeof(header));
    pwriteAll(fd, record.value.data(), record.value.size(), value_pos);

    write_offset = value_pos + record.value.size();
    metadata.size = write_offset;
    return Entry{metadata.file_id, value_pos, header.value_size};
}

std::string LogFile::read(const Entry& entry) const {
    if (fd == -1) throw std::runtime_error("Cannot read from a closed log file");
    if (entry.file_id != metadata.file_id) throw std::invalid_argument("Entry belongs to another log file");
    if (entry.value_pos > metadata.size || entry.value_size > metadata.size - entry.value_pos)
        throw std::runtime_error("Entry value is outside the log file");
    std::string value(entry.value_size, '\0');
    preadAll(fd, value.data(), value.size(), entry.value_pos);
    return value;
}

std::optional<Record> LogFile::readRecord(uint64_t offset) const {
    if (fd == -1) throw std::runtime_error("Cannot scan a closed log file");
    if (offset == metadata.size) return std::nullopt;
    if (offset > metadata.size || metadata.size - offset < sizeof(RecordHeader))
        throw std::runtime_error("Truncated record header");

    RecordHeader header{};
    preadAll(fd, &header, sizeof(header), offset);
    if (header.type != static_cast<uint8_t>(RecordType::Put) &&
        header.type != static_cast<uint8_t>(RecordType::Delete))
        throw std::runtime_error("Invalid record type");
    const uint64_t body_size = static_cast<uint64_t>(header.key_size) + header.value_size;
    if (body_size > metadata.size - offset - sizeof(header))
        throw std::runtime_error("Truncated record body");
    if (header.type == static_cast<uint8_t>(RecordType::Delete) && header.value_size != 0)
        throw std::runtime_error("Delete record has a value");

    Record record{header, std::string(header.key_size, '\0'), std::string(header.value_size, '\0')};
    uint64_t body_offset = offset + sizeof(header);
    preadAll(fd, record.key.data(), record.key.size(), body_offset);
    body_offset += record.key.size();
    preadAll(fd, record.value.data(), record.value.size(), body_offset);
    return record;
}

uint64_t LogFile::size() const { return metadata.size; }
uint32_t LogFile::fileId() const { return metadata.file_id; }

void LogFile::sync() {
    if (fd == -1) throw std::runtime_error("Cannot sync a closed log file");
    while (::fsync(fd) == -1) {
        if (errno == EINTR) continue;
        throw ioError("fsync failed");
    }
}
