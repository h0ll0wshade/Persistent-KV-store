#pragma once

// Provides shared-reader or exclusive-writer process locking for a database.

#include <string>

// Holds a process-level shared (reader) or exclusive (writer) directory lock.
class DatabaseLock {
public:
    DatabaseLock(const std::string& directory, bool read_only);
    ~DatabaseLock();

    DatabaseLock(const DatabaseLock&) = delete;
    DatabaseLock& operator=(const DatabaseLock&) = delete;

private:
    int fd{-1};
};
