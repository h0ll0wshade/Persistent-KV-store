#include "recovery_manager.h"

#include "kvdir.h"
#include "log_manager.h"
#include "logfile.h"

#include <cstdint>

void RecoveryManager::recover(LogManager& logs, KeyDir& keydir) const {
    keydir.clear();

    for (LogFile* log : logs.filesInOrder()) {
        uint64_t offset = 0;
        const uint64_t file_size = log->size();

        while (offset < file_size) {
            try {
                const auto record = log->readRecord(offset);
                if (!record) break;

                const uint64_t value_pos = offset + sizeof(RecordHeader) + record->header.key_size;
                if (record->header.type == static_cast<uint8_t>(RecordType::Put)) {
                    keydir.put(record->key,
                               Entry{log->fileId(), value_pos, record->header.value_size});
                } else {
                    keydir.erase(record->key);
                }

                offset += sizeof(RecordHeader) + record->header.key_size + record->header.value_size;
            } catch (const RecordFormatError&) {
                // This is best-effort resynchronization. The current native
                // record format has no marker or checksum to identify a boundary.
                ++offset;
            }
        }
    }
}
