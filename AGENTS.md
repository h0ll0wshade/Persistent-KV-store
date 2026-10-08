# Bitcask KV Store: Architecture and Development Guide

## Goal

Build a standalone persistent, log-structured key-value engine in C++17. Append-only disk logs are the source of truth; the in-memory index is derived state and can be rebuilt from the logs. Prioritize understandable code, correctness, crash safety, and testability. Implement one phase at a time and do not claim features work until they have been verified.

## Architecture

- **Record** describes a PUT value or DELETE tombstone. Each log record is serialized as a native-layout `RecordHeader`, key bytes, then value bytes. A later reliability phase will add a versioned format and checksums.
- **Entry** identifies a value by `file_id`, `value_pos`, and `value_size`. `value_pos` points to the first value byte, not the record or header.
- **LogFile** owns one POSIX file descriptor using RAII. It appends records and reads values or complete records with positional I/O. It handles short I/O and `EINTR`, detects invalid/truncated records, and is non-copyable with safe move semantics.
- **KeyDir** is an in-memory `unordered_map<string, Entry>` with average O(1) lookup. It does no disk I/O.
- **CrudOperations** handles the PUT/GET/DELETE/list/sync workflows. It coordinates `LogFile` and `KeyDir`, and updates the index only after a successful append.
- **LogManager** discovers and owns numbered log files, selects the highest ID as active, and routes reads by file ID. Rotation is not implemented yet.
- **RecoveryManager** rebuilds KeyDir at startup by replaying logs from byte zero in numeric file-ID order. Its scan cursor is temporary and is never saved.
- **KVStore** is the client-facing API facade. It forwards calls to `CrudOperations`; its private implementation only creates and owns the current component instances. It must not accumulate storage algorithms.

## Operation flows

- PUT asks `CrudOperations` to append a PUT record, then update KeyDir with the returned Entry, then apply the configured sync policy.
- GET asks `CrudOperations` to look up the Entry and read the value bytes from its log, returning an optional value.
- DELETE asks `CrudOperations` to append a tombstone, then remove the key from KeyDir.
- Recovery scans each discovered log from beginning to end. PUT records replace prior locations; DELETE records erase keys. If bytes are malformed, recovery advances one byte and searches for the next structurally plausible record.

## File lifecycle and compaction

- Name logs with zero-padded, monotonically increasing IDs, such as `000001.log`. Exactly one log is active; older logs are immutable. Rotate before an append would take the active file past the configured 64 MiB default.
- Recovery sorts discovered IDs numerically, reads records in serialized-size order, and restores the newest active log and its append position. A valid PUT updates the index to the value bytes; a tombstone removes the key.
- Manual compaction selects immutable logs, keeps only records whose locations are still the current live entries, writes replacement records to temporary output files, syncs and publishes all outputs, then removes source logs. A crash before publication leaves source logs intact; temporary files are ignored. A crash during cleanup is safe because published outputs contain the retained live values.
- Background compaction is a later opt-in feature and uses the same compaction path under exclusive coordination. The initial practical trigger is four or more immutable files; configuration can be added if measurements justify it.

## Target public API

The target API uses `put`, `get`, `erase`, `listKeys`, `sync`, `compact`, and `close`, with `fold` and `stats` added in the later API phase. Opening may use a constructor or `open(directory, options)`. Use `put`/`erase` as the primary names; do not add legacy `post`/`del` aliases. I/O and invalid database state are reported with exceptions. Optional results represent missing keys.

Options will include a 64 MiB default maximum log size, `sync_on_put = false`, `read_only = false`, and opt-in background compaction. A successful write with sync disabled is not promised durable until `sync()` or a later sync-enabled operation.

`main.cpp` is an interactive demo client, not part of the storage library. Keep it limited to parsing commands and calling the public `KVStore` API; do not put storage behavior there.

The final API also supports `fold(callback)` for live key/value iteration and `stats()` for file count, bytes, live-key count, and reclaimable-space estimates. A read-only open does not create or mutate database files. Process locking prevents a second writer from opening the same directory.

## Reliability, lifecycle, and concurrency targets

- Correct partial `pread`/`pwrite` handling, `EINTR` retries, descriptor ownership, and bounds checks.
- Startup recovery never modifies log files. It uses best-effort byte-by-byte resynchronization after malformed or incomplete bytes; without record framing or checksums, this can mistake data bytes for a record.
- Record checksums use CRC32 in the versioned final format. The current native header is transitional; development-only files do not require migration.
- Rotation maintains exactly one active file and leaves immutable files readable.
- Compaction processes immutable files, publishes complete outputs before deleting source files, and remains recoverable after interruption.
- A store-level shared mutex allows concurrent reads and serializes writes, rotation, and compaction. A POSIX directory lock enforces one writer process; read-only opens use a shared lock.
- Background compaction is disabled by default. The worker is stopped and joined during close/destruction.
- `close()` and destructors release resources through RAII. `fold` iterates live key/value pairs; `stats` reports storage metrics.

## Project layout

```text
AGENTS.md
CMakeLists.txt
includes/{records,entry,logfile,log_manager,recovery_manager,kvdir,crud_operations,kvstore,options}.h
src/{records.cpp,logfile.cpp,log_manager.cpp,recovery_manager.cpp,kvdir.cpp,crud_operations.cpp,kvstore.cpp,main.cpp}
tests/{test_record,test_logfile,test_log_manager,test_kvdir,test_crud_operations,test_kvstore,test_recovery,test_compaction,test_concurrency}.cpp
benchmarks/benchmark.cpp
README.md
```

Names may evolve when a clearer module boundary is needed. Keep the implementation dependency-light and C++17.

## Implementation roadmap

1. **Core storage:** CMake/CTest, Record and Entry foundations, robust LogFile, KeyDir, one `CrudOperations` component, and a forwarding KVStore facade. Verify serialization, offsets, value reads, index behavior, failed-append ordering, and CRUD.
2. **Persistence:** discover numbered logs, replay all records to rebuild the index, route reads by file ID, and test restart behavior. Recovery starts over from byte zero on every open and does not truncate or rewrite logs.
3. **Rotation and compaction:** keep log lifecycle and rotation together in a dedicated log-management component; add a separate compaction component for safe manual rewrites, then an independent background worker that schedules it.
4. **Reliability and concurrency:** keep checksum/encoding rules in the record codec, sync policy in durability handling, process locking in an RAII lock component, and thread coordination in a dedicated synchronization boundary.
5. **API and measurement:** add close/fold/stats, benchmark PUT throughput, GET latency percentiles, recovery and compaction time/space, thread scaling, sync overhead, and documentation of tradeoffs.

## Benchmark deliverables

The standalone benchmark reports PUT operations per second; GET p50, p95, and p99 latency; recovery time; compaction elapsed time and space reclaimed; throughput as thread count changes; and the cost of sync-on-write compared with buffered writes. Record the machine/compiler/build settings and workload sizes so results can be compared meaningfully.

## Current implementation boundary

The current implementation uses `KVStore` as a forwarding facade, `CrudOperations` for CRUD coordination, `LogManager` for the discovered log set, and `RecoveryManager` for startup replay. It scans every numbered log from byte zero and does not save scan positions or modify log contents. Resynchronization after malformed bytes is best-effort because the current native record format has no marker or checksum; a false record may be accepted. Rotation, compaction, checksums, process locking, and thread safety are not implemented yet.

## Development rules

- Preserve unrelated user changes. Inspect files before editing.
- Keep public interfaces consistent and document necessary changes.
- Keep internal components grouped by related responsibility; do not put CRUD, recovery, compaction, or worker scheduling into one class.
- Add a separate internal component when a distinct feature is implemented. Do not create placeholder managers for future phases.
- Test the component being implemented, including edge cases and failure behavior where practical.
- Do not implement later roadmap phases as part of a core-storage change.
- Document current limitations accurately.
