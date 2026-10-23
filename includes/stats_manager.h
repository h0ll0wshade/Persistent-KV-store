#pragma once

// Declares collection of storage metrics by comparing logs with the key index.

#include "kv_stats.h"

class KeyDir;
class LogManager;

// Computes physical and reclaimable storage metrics from logs and KeyDir.
class StatsManager {
public:
    StatsManager(const KeyDir& keydir, const LogManager& logs);
    KVStats collect() const;

private:
    const KeyDir& keydir;
    const LogManager& logs;
};
