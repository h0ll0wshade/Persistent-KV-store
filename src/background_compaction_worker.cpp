// Implements wake-up, failure reporting, and orderly shutdown for compaction work.
#include "background_compaction_worker.h"

#include <utility>

BackgroundCompactionWorker::BackgroundCompactionWorker(std::function<void()> callback)
    : work(std::move(callback)), thread(&BackgroundCompactionWorker::run, this) {}

BackgroundCompactionWorker::~BackgroundCompactionWorker() {
    stop();
}

void BackgroundCompactionWorker::notify() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || failure) return;
        requested = true;
    }
    changed.notify_one();
}

void BackgroundCompactionWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    changed.notify_one();
    if (thread.joinable()) thread.join();
}

void BackgroundCompactionWorker::rethrowIfFailed() const {
    std::exception_ptr error;
    {
        std::lock_guard<std::mutex> lock(mutex);
        error = failure;
    }
    if (error) std::rethrow_exception(error);
}

void BackgroundCompactionWorker::run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
        changed.wait(lock, [&] { return stopping || requested; });
        if (stopping) return;
        requested = false;
        lock.unlock();
        try {
            work();
        } catch (...) {
            lock.lock();
            failure = std::current_exception();
            stopping = true;
            return;
        }
        lock.lock();
    }
}
