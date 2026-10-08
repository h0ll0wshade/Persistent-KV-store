#include "kvstore.h"

#include "crud_operations.h"
#include "kvdir.h"
#include "logfile.h"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string prepareInitialLogPath(const std::string& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) throw std::runtime_error("Failed to create database directory: " + error.message());

    std::ostringstream filename;
    filename << std::setw(6) << std::setfill('0') << 1 << ".log";
    return (std::filesystem::path(directory) / filename.str()).string();
}
}

// Composition and lifetime owner only. Operation behavior lives in CrudOperations.
struct KVStore::Impl {
    KeyDir keydir;
    LogFile active_log;
    CrudOperations crud;

    explicit Impl(const std::string& directory)
        : keydir(), active_log(prepareInitialLogPath(directory), 1, true), crud(keydir, active_log) {}
};

KVStore::KVStore(const std::string& directory) : impl(std::make_unique<Impl>(directory)) {}

KVStore::~KVStore() = default;

void KVStore::put(const std::string& key, const std::string& value) {
    impl->crud.put(key, value);
}

std::optional<std::string> KVStore::get(const std::string& key) const {
    return impl->crud.get(key);
}

void KVStore::erase(const std::string& key) {
    impl->crud.erase(key);
}

std::vector<std::string> KVStore::listKeys() const {
    return impl->crud.listKeys();
}

void KVStore::sync() {
    impl->crud.sync();
}
