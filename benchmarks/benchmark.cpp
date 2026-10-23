// Measures store throughput, latency, recovery, compaction, threading, and sync cost.
#include "kvstore.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/utsname.h>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

class TemporaryDatabase {
public:
    TemporaryDatabase() {
        std::string pattern = (std::filesystem::temp_directory_path() / "bitcask-bench-XXXXXX").string();
        pattern.push_back('\0');
        char* result = ::mkdtemp(pattern.data());
        if (!result) throw std::runtime_error("mkdtemp failed");
        path_ = result;
    }
    ~TemporaryDatabase() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

size_t parseOperations(int argc, char** argv) {
    if (argc < 2) return 10000;
    const long parsed = std::strtol(argv[1], nullptr, 10);
    if (parsed <= 0) throw std::invalid_argument("operation count must be positive");
    return static_cast<size_t>(parsed);
}

double elapsedMilliseconds(Clock::time_point start) {
    return Milliseconds(Clock::now() - start).count();
}

std::string keyFor(size_t index) { return "key-" + std::to_string(index); }

double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) return 0;
    const size_t index = static_cast<size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

void runThreadedWrites(size_t operations, unsigned thread_count) {
    TemporaryDatabase directory;
    KVStore store(directory.path());
    const auto start = Clock::now();
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (unsigned thread = 0; thread < thread_count; ++thread) {
        threads.emplace_back([&, thread] {
            const size_t begin = operations * thread / thread_count;
            const size_t end = operations * (thread + 1) / thread_count;
            for (size_t i = begin; i < end; ++i)
                store.put("t" + std::to_string(thread) + "-" + keyFor(i), "benchmark-value");
        });
    }
    for (auto& thread : threads) thread.join();
    store.sync();
    const double milliseconds = elapsedMilliseconds(start);
    std::cout << "thread_scaling threads=" << thread_count << " operations=" << operations
              << " elapsed_ms=" << milliseconds
              << " ops_per_sec=" << operations * 1000.0 / milliseconds << '\n';
}
}

int main(int argc, char** argv) {
    try {
        const size_t operations = parseOperations(argc, argv);
        struct utsname system_info {};
        const std::string system = ::uname(&system_info) == 0
            ? std::string(system_info.sysname) + " " + system_info.release + " " + system_info.machine
            : "unknown";
        std::cout << "benchmark=bitcask_kv cxx=17 compiler=" << __VERSION__
                  << " system=\"" << system << "\" hardware_threads="
                  << std::thread::hardware_concurrency() << " operations=" << operations
                  << " value_bytes=16\n";

        TemporaryDatabase put_directory;
        {
            KVStore store(put_directory.path());
            const auto start = Clock::now();
            for (size_t i = 0; i < operations; ++i)
                store.put(keyFor(i), "benchmark-value");
            const double milliseconds = elapsedMilliseconds(start);
            store.sync();
            std::cout << "put operations=" << operations << " elapsed_ms=" << milliseconds
                      << " ops_per_sec=" << operations * 1000.0 / milliseconds << '\n';
        }

        TemporaryDatabase get_directory;
        std::vector<double> latencies;
        latencies.reserve(operations);
        {
            KVStore store(get_directory.path());
            for (size_t i = 0; i < operations; ++i)
                store.put(keyFor(i), "benchmark-value");
            for (size_t i = 0; i < operations; ++i) {
                const auto start = Clock::now();
                (void)store.get(keyFor(i));
                latencies.push_back(Milliseconds(Clock::now() - start).count());
            }
        }
        std::sort(latencies.begin(), latencies.end());
        std::cout << "get samples=" << operations << " p50_ms=" << percentile(latencies, 0.50)
                  << " p95_ms=" << percentile(latencies, 0.95)
                  << " p99_ms=" << percentile(latencies, 0.99) << '\n';

        TemporaryDatabase recovery_directory;
        {
            KVStore store(recovery_directory.path());
            for (size_t i = 0; i < operations; ++i)
                store.put(keyFor(i), "benchmark-value");
        }
        const auto recovery_start = Clock::now();
        {
            KVStore recovered(recovery_directory.path());
            (void)recovered.listKeys();
        }
        std::cout << "recovery records=" << operations
                  << " elapsed_ms=" << elapsedMilliseconds(recovery_start) << '\n';

        TemporaryDatabase compaction_directory;
        {
            Options options;
            options.max_file_size = 256 * 1024;
            KVStore store(compaction_directory.path(), options);
            const size_t live_keys = std::max<size_t>(1, std::min<size_t>(operations / 10, 1000));
            for (size_t i = 0; i < operations; ++i)
                store.put(keyFor(i % live_keys), "benchmark-value");
            const KVStats before = store.stats();
            const auto start = Clock::now();
            store.compact();
            const double milliseconds = elapsedMilliseconds(start);
            const KVStats after = store.stats();
            const uint64_t bytes_saved = before.total_bytes > after.total_bytes
                ? before.total_bytes - after.total_bytes : 0;
            std::cout << "compaction elapsed_ms=" << milliseconds << " bytes_before="
                      << before.total_bytes << " bytes_after=" << after.total_bytes
                      << " bytes_reclaimed=" << bytes_saved << " live_keys="
                      << after.live_key_count << '\n';
        }

        for (const unsigned threads : {1U, 2U, 4U})
            runThreadedWrites(operations, threads);

        const size_t sync_operations = std::min<size_t>(operations, 1000);
        for (const bool sync_on_put : {false, true}) {
            TemporaryDatabase directory;
            Options options;
            options.sync_on_put = sync_on_put;
            KVStore store(directory.path(), options);
            const auto start = Clock::now();
            for (size_t i = 0; i < sync_operations; ++i)
                store.put(keyFor(i), "benchmark-value");
            const double milliseconds = elapsedMilliseconds(start);
            std::cout << "sync_on_put=" << (sync_on_put ? "true" : "false")
                      << " operations=" << sync_operations << " elapsed_ms=" << milliseconds
                      << " ops_per_sec=" << sync_operations * 1000.0 / milliseconds << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
