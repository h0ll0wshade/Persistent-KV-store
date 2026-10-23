#pragma once

// Declares log replay that rebuilds the in-memory key index on open.

class KeyDir;
class LogManager;

// Rebuilds the in-memory index by replaying every log from byte zero.
class RecoveryManager {
public:
    void recover(LogManager& logs, KeyDir& keydir) const;
};
