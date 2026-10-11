#include "kvstore.h"

#include "compaction_manager.h"
#include "crud_operations.h"
#include "database_lock.h"
#include "kvdir.h"
#include "log_manager.h"
#include "recovery_manager.h"

// Composition and lifetime owner only. Operation behavior lives in CrudOperations.
struct KVStore::Impl {
    DatabaseLock process_lock;
    KeyDir keydir;
    LogManager logs;
    RecoveryManager recovery;
    CompactionManager compaction;
    CrudOperations crud;
    bool read_only;

    explicit Impl(const std::string& directory, const Options& options)
        : process_lock(directory, options.read_only), keydir(), logs(directory, options), recovery(),
          compaction(keydir, logs), crud(keydir, logs, options), read_only(options.read_only) {
        recovery.recover(logs, keydir);
    }

    void requireOpen() const {
        if (closed) throw std::runtime_error("KVStore is closed");
    }
    void requireWritable() const {
        requireOpen();
        if (read_only) throw std::runtime_error("KVStore is read-only");
    }
    bool closed{false};
};

KVStore::KVStore(const std::string& directory, const Options& options)
    : impl(std::make_unique<Impl>(directory, options)) {}

KVStore::~KVStore() = default;

void KVStore::put(const std::string& key, const std::string& value) {
    if (!impl) throw std::runtime_error("KVStore is closed");
    impl->requireWritable();
    impl->crud.put(key, value);
}

std::optional<std::string> KVStore::get(const std::string& key) const {
    if (!impl) throw std::runtime_error("KVStore is closed");
    impl->requireOpen();
    return impl->crud.get(key);
}

void KVStore::erase(const std::string& key) {
    if (!impl) throw std::runtime_error("KVStore is closed");
    impl->requireWritable();
    impl->crud.erase(key);
}

std::vector<std::string> KVStore::listKeys() const {
    if (!impl) throw std::runtime_error("KVStore is closed");
    impl->requireOpen();
    return impl->crud.listKeys();
}

void KVStore::sync() {
    if (!impl) throw std::runtime_error("KVStore is closed");
    impl->requireOpen();
    impl->crud.sync();
}

void KVStore::compact() {
    if (!impl) throw std::runtime_error("KVStore is closed");
    impl->requireWritable();
    impl->compaction.compact();
}

void KVStore::close() {
    if (!impl) return;
    impl->requireOpen();
    if (!impl->read_only) impl->crud.sync();
    impl->logs.closeAll();
    impl->closed = true;
    impl.reset();
}
