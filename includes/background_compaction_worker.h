#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

// Schedules compaction work independently from CRUD and compaction algorithms.
class BackgroundCompactionWorker {
public:
    explicit BackgroundCompactionWorker(std::function<void()> work);
    ~BackgroundCompactionWorker();

    BackgroundCompactionWorker(const BackgroundCompactionWorker&) = delete;
    BackgroundCompactionWorker& operator=(const BackgroundCompactionWorker&) = delete;

    void notify();
    void stop();
    void rethrowIfFailed() const;

private:
    void run();

    std::function<void()> work;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread thread;
    bool requested{false};
    bool stopping{false};
    std::exception_ptr failure;
};
