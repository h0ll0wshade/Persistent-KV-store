#pragma once

#include "kvdir.h"
#include "log_manager.h"

#include <optional>
#include <string>
#include <vector>

// Coordinates CRUD operations across the in-memory index and active log.
class CrudOperations {
public:
    CrudOperations(KeyDir& keydir, LogManager& logs);

    void put(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key) const;
    void erase(const std::string& key);
    std::vector<std::string> listKeys() const;
    void sync();

private:
    KeyDir& keydir;
    LogManager& logs;
};
