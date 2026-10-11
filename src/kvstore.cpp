#include "kvstore.h"

#include "compaction_manager.h"
#include "background_compaction_worker.h"
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
    std::unique_ptr<BackgroundCompactionWorker> worker;

    explicit Impl(const std::string& directory, const Options& options,
                  StoreConcurrency& store_concurrency)
        : process_lock(directory, options.read_only), keydir(), logs(directory, options), recovery(),
          compaction(keydir, logs), crud(keydir, logs, options), read_only(options.read_only) {
        recovery.recover(logs, keydir);
        if (options.enable_background_compaction && !read_only) {
            worker = std::make_unique<BackgroundCompactionWorker>([this, &store_concurrency] {
                store_concurrency.withWrite([this] {
                    if (!closed && logs.immutableFileCount() >= 4) compaction.compact();
                });
            });
        }
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
    : concurrency(), close_mutex(), impl(std::make_unique<Impl>(directory, options, concurrency)) {}

KVStore::~KVStore() = default;

void KVStore::put(const std::string& key, const std::string& value) {
    concurrency.withWrite([&] {
        if (!impl) throw std::runtime_error("KVStore is closed");
        impl->requireWritable();
        if (impl->worker) impl->worker->rethrowIfFailed();
        impl->crud.put(key, value);
        if (impl->worker) impl->worker->notify();
    });
}

std::optional<std::string> KVStore::get(const std::string& key) const {
    return concurrency.withRead([&] {
        if (!impl) throw std::runtime_error("KVStore is closed");
        impl->requireOpen();
        if (impl->worker) impl->worker->rethrowIfFailed();
        return impl->crud.get(key);
    });
}

void KVStore::erase(const std::string& key) {
    concurrency.withWrite([&] {
        if (!impl) throw std::runtime_error("KVStore is closed");
        impl->requireWritable();
        if (impl->worker) impl->worker->rethrowIfFailed();
        impl->crud.erase(key);
        if (impl->worker) impl->worker->notify();
    });
}

std::vector<std::string> KVStore::listKeys() const {
    return concurrency.withRead([&] {
        if (!impl) throw std::runtime_error("KVStore is closed");
        impl->requireOpen();
        if (impl->worker) impl->worker->rethrowIfFailed();
        return impl->crud.listKeys();
    });
}

void KVStore::sync() {
    concurrency.withWrite([&] {
        if (!impl) throw std::runtime_error("KVStore is closed");
        impl->requireOpen();
        if (impl->worker) impl->worker->rethrowIfFailed();
        impl->crud.sync();
    });
}

void KVStore::compact() {
    concurrency.withWrite([&] {
        if (!impl) throw std::runtime_error("KVStore is closed");
        impl->requireWritable();
        if (impl->worker) impl->worker->rethrowIfFailed();
        impl->compaction.compact();
    });
}

void KVStore::close() {
    std::lock_guard<std::mutex> close_guard(close_mutex);
    if (impl && impl->worker) impl->worker->stop();
    std::exception_ptr worker_error;
    concurrency.withWrite([&] {
        if (!impl) return;
        impl->requireOpen();
        if (impl->worker) {
            try { impl->worker->rethrowIfFailed(); }
            catch (...) { worker_error = std::current_exception(); }
        }
        if (!impl->read_only) impl->crud.sync();
        impl->logs.closeAll();
        impl->closed = true;
        impl.reset();
    });
    if (worker_error) std::rethrow_exception(worker_error);
}
