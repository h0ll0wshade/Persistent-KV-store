#pragma once

#include "kvdir.h"
#include "logfile.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class KVStore {
private:
    KeyDir keydir;

    std::vector<std::unique_ptr<LogFile>> logFiles;

    size_t log_sz;

public:
    KVStore(const std::string& directory);
    ~KVStore();

    void post(const std::string& key, const std::string& value);

    std::string get(const std::string& key);

    void del(const std::string& key);

    std::vector<std::string> listKeys();
};