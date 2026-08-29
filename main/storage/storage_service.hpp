#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <esp_err.h>

#include "hal_storage.hpp"

#define STORAGE_REQUEST_QUEUE_LENGTH 4U
#define STORAGE_RESULT_QUEUE_LENGTH 8U
#define STORAGE_WORKER_TASK_STACK_SIZE 6144U
#define STORAGE_WORKER_TASK_PRIORITY 4U
#define STORAGE_MAX_DIRECTORY_ENTRIES 1024U
#define STORAGE_INITIAL_ENTRY_CAPACITY 64U
#define STORAGE_MAX_PATH_LENGTH 512U
#define STORAGE_MAX_FILE_NAME_LENGTH 255U

enum class file_entry_type : std::uint8_t {
    directory,
    file,
};

struct file_entry {
    std::string name;
    file_entry_type type;
    std::uint64_t size;
};

enum class storage_result_code : std::uint8_t {
    ok,
    no_card,
    cancelled,
    invalid_path,
    too_many_entries,
    io_error,
    no_memory,
};

struct storage_directory_result {
    std::uint32_t request_id;
    std::uint32_t session_id;
    std::uint32_t media_generation;
    storage_result_code code;
    std::string path;
    std::vector<file_entry> entries;
};

enum class storage_event_type : std::uint8_t {
    directory_result,
};

struct storage_event {
    storage_event_type type;
    storage_directory_result* directory_result;
};

static_assert(std::is_trivially_copyable_v<storage_event>);
static_assert(STORAGE_MAX_PATH_LENGTH <= SD_PATH_LENGTH);
static_assert(STORAGE_MAX_FILE_NAME_LENGTH + 1U <= SD_FILE_NAME_LENGTH);

// Starts the single worker that owns asynchronous directory scans.
esp_err_t storage_service_init();

// Normalizes and queues a scan request without blocking the Mooncake task.
bool storage_service_list_directory(
    const char* path,
    std::uint32_t request_id,
    std::uint32_t session_id);

// Transfers one result pointer to InputManager; release is mandatory after dispatch.
bool storage_service_try_get_event(storage_event& event);
void storage_service_release_event(storage_event& event);

