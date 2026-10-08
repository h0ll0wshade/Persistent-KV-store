#pragma once

class KeyDir;
class LogManager;

// Rebuilds the in-memory index by replaying every log from byte zero.
class RecoveryManager {
public:
    void recover(LogManager& logs, KeyDir& keydir) const;
};
