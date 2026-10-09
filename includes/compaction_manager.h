#pragma once

class KeyDir;
class LogManager;

// Rewrites immutable logs, keeping only the current live PUT records.
class CompactionManager {
public:
    CompactionManager(KeyDir& keydir, LogManager& logs);
    void compact();

private:
    KeyDir& keydir;
    LogManager& logs;
};
