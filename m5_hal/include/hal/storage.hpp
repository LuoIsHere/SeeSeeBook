#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

#include <esp_err.h>

#define SD_MOUNT_POINT "/sdcard"
#define SD_MAX_OPEN_FILES 4U
#define SD_PATH_LENGTH 512U
#define SD_FILE_NAME_LENGTH 256U

struct hal_storage_directory;

struct hal_storage_entry {
    char name[SD_FILE_NAME_LENGTH];
    std::uint64_t size;
    bool directory;
};

static_assert(std::is_trivially_copyable_v<hal_storage_entry>);

// Configures storage power and card-detect pins.
bool hal_storage_init();

// Reads card insertion after board-specific active-level normalization.
bool hal_storage_card_inserted(bool& inserted);

esp_err_t hal_storage_mount();
esp_err_t hal_storage_unmount();

// Directory handles retain the filesystem mutex until close.
esp_err_t hal_storage_open_directory(
    const char* path,
    hal_storage_directory*& directory);
esp_err_t hal_storage_read_directory(
    hal_storage_directory* directory,
    hal_storage_entry& entry,
    bool& end_reached);
void hal_storage_close_directory(hal_storage_directory*& directory);

// One bounded transaction. No file handle outlives the filesystem mutex.
esp_err_t hal_storage_read_file_chunk(
    const char* path, std::uint64_t offset, char* data, std::size_t capacity,
    std::size_t& length, std::uint64_t& file_size, std::int64_t& modified_time);

// Bounded internal-data operations. Each opens/closes under the filesystem lock.
esp_err_t hal_storage_ensure_system_directory(const char* path);
esp_err_t hal_storage_write_system_file(
    const char* path, std::uint64_t offset, const void* data, std::size_t length, bool truncate);
esp_err_t hal_storage_replace_system_file(const char* temporary, const char* destination);
