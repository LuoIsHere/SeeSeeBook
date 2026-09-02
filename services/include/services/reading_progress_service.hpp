#pragma once

#include <cstdint>

#include <esp_err.h>

struct reading_file_identity {
    std::uint8_t path_digest[32];
    std::uint64_t file_size;
    std::int64_t modified_time;
};

// Main-task API. The service owns a bounded cache of the last 16 saved books.
// Initialization never erases NVS on failure; reading can continue in RAM.
bool reading_progress_identify(const char* canonical_path, reading_file_identity& identity);
bool reading_progress_load(const reading_file_identity& identity, std::uint64_t& offset);
esp_err_t reading_progress_save(const reading_file_identity& identity, std::uint64_t offset);
bool reading_progress_is_persistent();
