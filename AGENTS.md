# Bitcask KV Store: Architecture and Development Guide

## Goal

Build a standalone persistent, log-structured key-value engine in C++17. Append-only disk logs are the source of truth; the in-memory index is derived state and can be rebuilt from the logs. Prioritize understandable code, correctness, crash safety, and testability. Implement one phase at a time and do not claim features work until they have been verified.

## Architecture

- **Record** describes a PUT value or DELETE tombstone. `RecordCodec` serializes each record into a fixed-width little-endian header with magic, version, lengths, CRC32 checksum, key bytes, and value bytes. `RecordHeader` is only an in-memory description.
- **Entry** identifies a value by `file_id`, `value_pos`, and `value_size`. `value_pos` points to the first value byte, not the record or header.
- **LogFile** owns one POSIX file descriptor using RAII. It appends records and reads values or complete records with positional I/O. It handles short I/O and `EINTR`, detects invalid/truncated records, and is non-copyable with safe move semantics.
- **KeyDir** is an in-memory `unordered_map<string, Entry>` with average O(1) lookup. It does no disk I/O.
- **CrudOperations** handles the PUT/GET/DELETE/list/sync workflows. It coordinates `LogFile` and `KeyDir`, and updates the index only after a successful append.
- **LogManager** discovers and owns numbered log files, selects the highest ID as active, routes reads by file ID, and rotates before an append would exceed the configured size limit.
- **CompactionManager** performs synchronous compaction separately from CRUD and log discovery. It copies only records whose value locations still match KeyDir, publishes outputs before removing source logs, and syncs directory changes.
- **RecoveryManager** rebuilds KeyDir at startup by replaying logs from byte zero in numeric file-ID order. Its scan cursor is temporary and is never saved.
- **KVStore** is the client-facing API facade. It forwards calls to `CrudOperations`; its private implementation only creates and owns the current component instances. It must not accumulate storage algorithms.

## Operation flows

- PUT asks `CrudOperations` to append a PUT record, then update KeyDir with the returned Entry, then apply the configured sync policy.
- GET asks `CrudOperations` to look up the Entry and read the value bytes from its log, returning an optional value.
- DELETE asks `CrudOperations` to append a tombstone, then remove the key from KeyDir.
- Recovery scans each discovered log from beginning to end. PUT records replace prior locations; DELETE records erase keys. If bytes are malformed or fail CRC32 validation, recovery advances one byte and searches for the next valid checksummed record.

## File lifecycle and compaction

- Name logs with zero-padded, monotonically increasing IDs, such as `000001.log`. Exactly one log is active; older logs are immutable. Rotate before an append would take the active file past the configured 64 MiB default. An oversized record is allowed in an empty log and the following append rotates.
- Recovery sorts discovered IDs numerically, reads records in serialized-size order, and restores the newest active log and its append position. A valid PUT updates the index to the value bytes; a tombstone removes the key.
- Manual compaction first rotates the active log, then selects all immutable logs, keeps only records whose locations are still the current live entries, writes replacement records to temporary output files, syncs and publishes all outputs, and syncs the directory before updating KeyDir. It removes source logs in ascending file-ID order and syncs the directory after each unlink. A crash before publication leaves source logs intact; temporary files are ignored. During cleanup, published live values override old versions, while ordered durable deletion prevents old values from resurfacing after a tombstone is removed.
- Background compaction is a later opt-in feature and uses the same compaction path under exclusive coordination. The initial practical trigger is four or more immutable files; configuration can be added if measurements justify it.

## Target public API

The target API uses `put`, `get`, `erase`, `listKeys`, `sync`, `compact`, and `close`, with `fold` and `stats` added in the later API phase. Opening may use a constructor or `open(directory, options)`. Use `put`/`erase` as the primary names; do not add legacy `post`/`del` aliases. I/O and invalid database state are reported with exceptions. Optional results represent missing keys.

Options include a 64 MiB default maximum log size, `sync_on_put = false`, and `read_only = false`. A successful mutation with sync disabled is not promised durable until `sync()`; `sync_on_put` flushes the active log after each mutation. Explicit sync flushes all log descriptors and the database directory. Directory metadata is synced after database-directory creation, log creation, compaction output publication, and each source-file removal. A `FORMAT` marker identifies the versioned checksummed format; opening populated databases without a supported marker fails without modifying their logs. Read-only opens reject mutation APIs, and a process-level `.lock` file coordinates shared readers and exclusive writers.

`main.cpp` is an interactive demo client, not part of the storage library. Keep it limited to parsing commands and calling the public `KVStore` API; do not put storage behavior there.

The final API also supports `fold(callback)` for live key/value iteration and `stats()` for file count, bytes, live-key count, and reclaimable-space estimates. A read-only open does not create or mutate database files. Process locking prevents a second writer from opening the same directory.

## Reliability, lifecycle, and concurrency targets

- Correct partial `pread`/`pwrite` handling, `EINTR` retries, descriptor ownership, and bounds checks.
- Startup recovery never modifies log files. It uses best-effort byte-by-byte resynchronization after malformed or incomplete bytes. Magic, version, structural checks, and CRC32 reduce false record matches, but recovery cannot prove arbitrary bytes are not a valid record.
- Records use a fixed-width little-endian versioned format with CRC32. Populated legacy native-layout databases are rejected pending an explicit migration tool; empty unmarked databases can be initialized.
- Rotation maintains exactly one active file and leaves immutable files readable.
- Compaction processes immutable files, publishes complete outputs before deleting source files, and remains recoverable after interruption.
- A store-level shared mutex allows concurrent reads and serializes writes, rotation, and compaction. A POSIX `.lock` file enforces one writer process; read-only opens use a shared lock.
- Background compaction is disabled by default. The worker is stopped and joined during close/destruction.
- `close()` and destructors release resources through RAII. `fold` iterates live key/value pairs; `stats` reports storage metrics.

## Project layout

```text
AGENTS.md
CMakeLists.txt
includes/{records,record_codec,entry,logfile,log_manager,recovery_manager,kvdir,crud_operations,kvstore,options,database_lock,store_concurrency}.h
src/{records.cpp,record_codec.cpp,logfile.cpp,log_manager.cpp,recovery_manager.cpp,kvdir.cpp,crud_operations.cpp,kvstore.cpp,database_lock.cpp,main.cpp}
tests/{test_record,test_record_codec,test_logfile,test_log_manager,test_kvdir,test_crud_operations,test_kvstore,test_recovery,test_compaction,test_concurrency}.cpp
benchmarks/benchmark.cpp
README.md
```

Names may evolve when a clearer module boundary is needed. Keep the implementation dependency-light and C++17.

## Implementation roadmap

1. **Core storage:** CMake/CTest, Record and Entry foundations, robust LogFile, KeyDir, one `CrudOperations` component, and a forwarding KVStore facade. Verify serialization, offsets, value reads, index behavior, failed-append ordering, and CRUD.
2. **Persistence:** discover numbered logs, replay all records to rebuild the index, route reads by file ID, and test restart behavior. Recovery starts over from byte zero on every open and does not truncate or rewrite logs.
3. **Rotation and compaction:** log rotation and synchronous manual compaction are implemented in separate `LogManager` and `CompactionManager` components; add an independent background worker that schedules compaction later.
4. **Reliability and concurrency:** keep checksum/encoding rules in the record codec, sync policy in durability handling, process locking in an RAII lock component, and thread coordination in a dedicated synchronization boundary.
5. **API and measurement:** add close/fold/stats, benchmark PUT throughput, GET latency percentiles, recovery and compaction time/space, thread scaling, sync overhead, and documentation of tradeoffs.

## Benchmark deliverables

The standalone benchmark reports PUT operations per second; GET p50, p95, and p99 latency; recovery time; compaction elapsed time and space reclaimed; throughput as thread count changes; and the cost of sync-on-write compared with buffered writes. Record the machine/compiler/build settings and workload sizes so results can be compared meaningfully.

## Current implementation boundary

The current implementation uses `KVStore` as a forwarding facade, `CrudOperations` for CRUD coordination, `LogManager` for the discovered log set and rotation, `CompactionManager` for manual compaction, `RecoveryManager` for startup replay, and `RecordCodec` for a fixed-width little-endian versioned record format with CRC32. A `FORMAT` marker prevents accidental interpretation of populated legacy databases. It scans every numbered log from byte zero and does not save scan positions or modify log contents during recovery. Resynchronization remains best-effort, although record magic, version, structural checks, and checksums reject most damaged candidates. Read-only open, process locking, and explicit idempotent `close()` are implemented. `StoreConcurrency` permits concurrent reads and serializes mutations, sync, compaction, and close. Background compaction, fold, stats, and benchmarks are not implemented yet. `Options::max_file_size` defaults to 64 MiB and `Options::sync_on_put` defaults to false. With sync-on-put enabled, PUT and DELETE tombstone writes fsync their active log before returning. Explicit `sync()` flushes every log and directory metadata. Directory entries are synced during initial/rotated log creation and compaction publication/cleanup.

## Development rules

- Preserve unrelated user changes. Inspect files before editing.
- Keep public interfaces consistent and document necessary changes.
- Keep internal components grouped by related responsibility; do not put CRUD, recovery, compaction, or worker scheduling into one class.
- Add a separate internal component when a distinct feature is implemented. Do not create placeholder managers for future phases.
- Test the component being implemented, including edge cases and failure behavior where practical.
- Do not implement later roadmap phases as part of a core-storage change.
- Document current limitations accurately.
