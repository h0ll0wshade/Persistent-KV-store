#include "kvstore.h"

#include "crud_operations.h"
#include "kvdir.h"
#include "log_manager.h"
#include "recovery_manager.h"

// Composition and lifetime owner only. Operation behavior lives in CrudOperations.
struct KVStore::Impl {
    KeyDir keydir;
    LogManager logs;
    RecoveryManager recovery;
    CrudOperations crud;

    explicit Impl(const std::string& directory)
        : keydir(), logs(directory), recovery(), crud(keydir, logs) {
        recovery.recover(logs, keydir);
    }
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
