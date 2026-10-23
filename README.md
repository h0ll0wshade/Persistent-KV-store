# Bitcask KV Store

A small persistent key-value store in C++17. It writes updates to append-only log files and rebuilds its in-memory index from those logs when reopened.

Features include log rotation, tombstones, checksummed records, crash recovery, manual and optional background compaction, configurable syncing, read-only opens, process locking, and thread-safe access.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Try the demo

```sh
./build/bitcask_demo ./data
```

Commands: `put <key> <value>`, `get <key>`, `erase <key>`, `list`, `logs`, `sync`, and `quit`. Values may contain spaces. Logs and database metadata are stored in the directory you provide.

## Library API

```cpp
#include "kvstore.h"

KVStore store("./data");
store.put("color", "blue");
auto value = store.get("color");
store.erase("color");
store.sync();
```

`get()` returns `std::optional<std::string>`. `close()` closes the store; destruction also releases resources. By default, writes are buffered by the OS and are not guaranteed durable until `sync()`; set `Options::sync_on_put = true` to sync every mutation. Background compaction is off by default. Set `Options::read_only = true` to open an existing database without modifying its data files.

## Benchmark

```sh
cmake -S . -B build-bench -DBITCASK_BUILD_BENCHMARKS=ON -DBUILD_TESTING=OFF
cmake --build build-bench
./build-bench/bitcask_benchmark 10000
```

The optional benchmark reports throughput, GET latency percentiles, recovery and compaction results, thread scaling, and sync-on-write overhead.

## Format note

Records use a versioned checksummed format. Populated databases created with the older native-layout format are rejected; migration is not currently provided.
