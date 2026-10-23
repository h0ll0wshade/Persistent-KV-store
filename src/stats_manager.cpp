// Calculates live and reclaimable bytes by scanning log contents.
#include "stats_manager.h"

#include "kvdir.h"
#include "log_manager.h"
#include "logfile.h"
#include "record_codec.h"

#include <algorithm>

StatsManager::StatsManager(const KeyDir& index, const LogManager& log_manager)
    : keydir(index), logs(log_manager) {}

KVStats StatsManager::collect() const {
    KVStats stats;
    stats.live_key_count = keydir.listKeys().size();
    const auto files = logs.filesInOrder();
    stats.file_count = files.size();
    uint64_t live_bytes = 0;

    for (const LogFile* log : files) {
        stats.total_bytes += log->size();
        uint64_t offset = 0;
        while (offset < log->size()) {
            try {
                const auto record = log->readRecord(offset);
                if (!record) break;
                const uint64_t record_size = RecordCodec::kHeaderSize +
                    static_cast<uint64_t>(record->header.key_size) + record->header.value_size;
                if (record->header.type == static_cast<uint8_t>(RecordType::Put)) {
                    const uint64_t value_pos = offset + RecordCodec::kHeaderSize + record->header.key_size;
                    const auto current = keydir.get(record->key);
                    if (current && current->file_id == log->fileId() &&
                        current->value_pos == value_pos && current->value_size == record->header.value_size)
                        live_bytes += record_size;
                }
                offset += record_size;
            } catch (const RecordFormatError&) {
                ++offset;
            }
        }
    }
    stats.reclaimable_bytes = stats.total_bytes > live_bytes ? stats.total_bytes - live_bytes : 0;
    return stats;
}
