#pragma once

#include <cstdint>
#include <type_traits>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SD_DETECT_STABLE_SAMPLE_COUNT 3U
#define SD_STATUS_EVENT_QUEUE_LENGTH 8U
#define SD_MONITOR_TASK_STACK_SIZE 4096U
#define SD_MONITOR_TASK_PRIORITY 5U
#define SD_FILESYSTEM_LOCK_TIMEOUT_MS 1000U
#define SD_INTERNAL_I2C_TIMEOUT_MS 20U
#define SD_POWER_SETTLE_TIME_MS 10U
#define SD_MOUNT_POINT "/sdcard"
#define SD_MAX_OPEN_FILES 4U
#define SD_PATH_LENGTH 512U
#define SD_FILE_NAME_LENGTH 256U

enum class sd_state : std::uint8_t {
    no_card,
    mounting,
    ready,
    error,
};

struct sd_status_event {
    sd_state state;
    std::uint32_t media_generation;
    esp_err_t error;
};

struct hal_storage_directory;

struct hal_storage_entry {
    char name[SD_FILE_NAME_LENGTH];
    std::uint64_t size;
    bool directory;
};

static_assert(std::is_trivially_copyable_v<sd_status_event>);
static_assert(std::is_trivially_copyable_v<hal_storage_entry>);

// Starts the system-level card-detect state machine and owns mount transitions.
bool hal_storage_start(TaskHandle_t& task_handle);

// Reads one state transition without blocking the main dispatcher.
bool hal_try_get_storage_status_event(sd_status_event& event);

sd_state hal_get_storage_state();
std::uint32_t hal_get_storage_media_generation();

// Directory handles retain the filesystem lock until close and are worker-task only.
esp_err_t hal_storage_open_directory(
    const char* path,
    std::uint32_t media_generation,
    hal_storage_directory*& directory);
esp_err_t hal_storage_read_directory(
    hal_storage_directory* directory,
    hal_storage_entry& entry,
    bool& end_reached);
void hal_storage_close_directory(hal_storage_directory*& directory);
