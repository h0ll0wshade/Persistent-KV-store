// Flushes directory metadata so file creation and removal survive a crash.
#include "directory_sync.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

void syncDirectory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error(std::string("open directory for sync failed: ") +
                                 std::strerror(errno));
    }

    int result;
    do {
        result = ::fsync(fd);
    } while (result == -1 && errno == EINTR);

    const int saved_errno = errno;
    ::close(fd);
    if (result == -1) {
        throw std::runtime_error(std::string("directory fsync failed: ") +
                                 std::strerror(saved_errno));
    }
}
