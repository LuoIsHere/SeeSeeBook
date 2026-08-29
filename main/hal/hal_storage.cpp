#include "hal_storage.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <new>
#include <sys/stat.h>

#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sdmmc_cmd.h>

#include "hal_internal_i2c.hpp"

struct hal_storage_directory {
    DIR* stream;
    std::uint32_t media_generation;
    char full_path[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)];
};

namespace {

constexpr char log_tag[] = "hal_storage";

QueueHandle_t status_event_queue = nullptr;
SemaphoreHandle_t filesystem_mutex = nullptr;
std::atomic<sd_state> current_state{sd_state::no_card};
std::atomic<std::uint32_t> current_media_generation{0U};
sdmmc_card_t* mounted_card = nullptr;
bool unmount_pending = false;

void publish_status(sd_state state, esp_err_t error = ESP_OK)
{
    current_state.store(state, std::memory_order_release);
    sd_status_event event = {};
    event.state = state;
    event.media_generation = current_media_generation.load(std::memory_order_acquire);
    event.error = error;
    if (xQueueSend(status_event_queue, &event, 0) != pdTRUE) {
        sd_status_event discarded = {};
        xQueueReceive(status_event_queue, &discarded, 0);
        xQueueSend(status_event_queue, &event, 0);
        ESP_LOGW(log_tag, "status event queue full; oldest event discarded");
    }
}

bool configure_sd_io()
{
    internal_i2c_guard bus_guard(SD_INTERNAL_I2C_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    auto& io_expander = M5.getIOExpander(0);
    const bool configured =
        io_expander.setHighImpedance(m5::M5IOE1_Class::gpio14, false) &&
        io_expander.setDirection(m5::M5IOE1_Class::gpio14, true) &&
        io_expander.digitalWrite(m5::M5IOE1_Class::gpio14, true) &&
        io_expander.setDirection(m5::M5IOE1_Class::gpio1, false);
    if (!configured) {
        ESP_LOGW(log_tag, "failed to configure TF_EN or TF_DET through M5IOE1");
    }
    return configured;
}

bool read_card_inserted(bool& inserted)
{
    internal_i2c_guard bus_guard(SD_INTERNAL_I2C_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    bool detect_level = true;
    if (!M5.getIOExpander(0).getInputLevel(
            m5::M5IOE1_Class::gpio1,
            &detect_level)) {
        ESP_LOGW(log_tag, "TF_DET read failed");
        return false;
    }
    // PaperMono TF_DET is active low while the SD power domain is enabled.
    inserted = !detect_level;
    return true;
}

esp_err_t mount_card()
{
    if (xSemaphoreTake(
            filesystem_mutex,
            pdMS_TO_TICKS(SD_FILESYSTEM_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    mounted_card = nullptr;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    const int clk = M5.getPin(m5::pin_name_t::sd_mmc_clk);
    const int cmd = M5.getPin(m5::pin_name_t::sd_mmc_cmd);
    const int d0 = M5.getPin(m5::pin_name_t::sd_mmc_d0);
    const int d1 = M5.getPin(m5::pin_name_t::sd_mmc_d1);
    const int d2 = M5.getPin(m5::pin_name_t::sd_mmc_d2);
    const int d3 = M5.getPin(m5::pin_name_t::sd_mmc_d3);
    if (clk < 0 || cmd < 0 || d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0) {
        xSemaphoreGive(filesystem_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    slot.clk = static_cast<gpio_num_t>(clk);
    slot.cmd = static_cast<gpio_num_t>(cmd);
    slot.d0 = static_cast<gpio_num_t>(d0);
    slot.d1 = static_cast<gpio_num_t>(d1);
    slot.d2 = static_cast<gpio_num_t>(d2);
    slot.d3 = static_cast<gpio_num_t>(d3);
    slot.width = 4;

    esp_vfs_fat_mount_config_t mount_config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = SD_MAX_OPEN_FILES;
    mount_config.disk_status_check_enable = true;
    const esp_err_t result = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT,
        &host,
        &slot,
        &mount_config,
        &mounted_card);
    xSemaphoreGive(filesystem_mutex);
    return result;
}

bool try_unmount_card()
{
    if (mounted_card == nullptr) {
        unmount_pending = false;
        return true;
    }
    if (xSemaphoreTake(
            filesystem_mutex,
            pdMS_TO_TICKS(SD_FILESYSTEM_LOCK_TIMEOUT_MS)) != pdTRUE) {
        unmount_pending = true;
        ESP_LOGW(log_tag, "unmount deferred; filesystem worker still active");
        return false;
    }

    const esp_err_t result = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, mounted_card);
    if (result != ESP_OK) {
        ESP_LOGW(log_tag, "SD unmount failed error=%s", esp_err_to_name(result));
        if (result == ESP_ERR_INVALID_STATE) {
            mounted_card = nullptr;
            unmount_pending = false;
            xSemaphoreGive(filesystem_mutex);
            return true;
        }
        unmount_pending = true;
        xSemaphoreGive(filesystem_mutex);
        return false;
    }
    mounted_card = nullptr;
    unmount_pending = false;
    xSemaphoreGive(filesystem_mutex);
    return true;
}

void handle_stable_card_change(bool inserted)
{
    current_media_generation.fetch_add(1U, std::memory_order_acq_rel);
    if (!inserted) {
        // Invalidate directory workers before waiting for their filesystem lock.
        publish_status(sd_state::no_card);
        try_unmount_card();
        ESP_LOGI(log_tag, "card removed generation=%lu",
            static_cast<unsigned long>(current_media_generation.load()));
        return;
    }

    if (unmount_pending && !try_unmount_card()) {
        publish_status(sd_state::error, ESP_ERR_INVALID_STATE);
        return;
    }
    publish_status(sd_state::mounting);
    const esp_err_t result = mount_card();
    if (result == ESP_OK) {
        publish_status(sd_state::ready);
        ESP_LOGI(log_tag, "card mounted generation=%lu",
            static_cast<unsigned long>(current_media_generation.load()));
    } else {
        publish_status(sd_state::error, result);
        ESP_LOGE(log_tag, "card mount failed error=%s", esp_err_to_name(result));
    }
}

void storage_monitor_task(void*)
{
    bool stable_inserted = false;
    bool candidate_inserted = false;
    bool stable_state_known = false;
    std::uint8_t matching_samples = 0U;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (unmount_pending) {
            try_unmount_card();
        }

        bool inserted = false;
        if (!read_card_inserted(inserted)) {
            matching_samples = 0U;
            continue;
        }
        if (matching_samples == 0U || inserted != candidate_inserted) {
            candidate_inserted = inserted;
            matching_samples = 1U;
        } else if (matching_samples < SD_DETECT_STABLE_SAMPLE_COUNT) {
            ++matching_samples;
        }
        if (matching_samples < SD_DETECT_STABLE_SAMPLE_COUNT ||
            (stable_state_known && candidate_inserted == stable_inserted)) {
            continue;
        }

        stable_inserted = candidate_inserted;
        stable_state_known = true;
        handle_stable_card_change(stable_inserted);
    }
}

bool build_full_path(const char* path, char* output, std::size_t output_size)
{
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    const int written = std::snprintf(output, output_size, "%s%s", SD_MOUNT_POINT, path);
    return written >= 0 && static_cast<std::size_t>(written) < output_size;
}

}  // namespace

bool hal_storage_start(TaskHandle_t& task_handle)
{
    status_event_queue = xQueueCreate(SD_STATUS_EVENT_QUEUE_LENGTH, sizeof(sd_status_event));
    filesystem_mutex = xSemaphoreCreateMutex();
    if (status_event_queue == nullptr || filesystem_mutex == nullptr) {
        return false;
    }
    if (!configure_sd_io()) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_SETTLE_TIME_MS));

    const bool started = xTaskCreate(
                             storage_monitor_task,
                             "sd_monitor",
                             SD_MONITOR_TASK_STACK_SIZE,
                             nullptr,
                             SD_MONITOR_TASK_PRIORITY,
                             &task_handle) == pdPASS;
    if (started) {
        ESP_LOGI(log_tag, "SD monitor started; TF_DET active_low=1");
    }
    return started;
}

bool hal_try_get_storage_status_event(sd_status_event& event)
{
    return status_event_queue != nullptr &&
           xQueueReceive(status_event_queue, &event, 0) == pdTRUE;
}

sd_state hal_get_storage_state()
{
    return current_state.load(std::memory_order_acquire);
}

std::uint32_t hal_get_storage_media_generation()
{
    return current_media_generation.load(std::memory_order_acquire);
}

esp_err_t hal_storage_open_directory(
    const char* path,
    std::uint32_t media_generation,
    hal_storage_directory*& directory)
{
    directory = nullptr;
    if (hal_get_storage_state() != sd_state::ready ||
        media_generation != hal_get_storage_media_generation()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(
            filesystem_mutex,
            pdMS_TO_TICKS(SD_FILESYSTEM_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    auto* new_directory = new (std::nothrow) hal_storage_directory{};
    if (new_directory == nullptr ||
        !build_full_path(path, new_directory->full_path, sizeof(new_directory->full_path))) {
        delete new_directory;
        xSemaphoreGive(filesystem_mutex);
        return new_directory == nullptr ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_SIZE;
    }
    new_directory->stream = opendir(new_directory->full_path);
    if (new_directory->stream == nullptr) {
        const int open_error = errno;
        delete new_directory;
        xSemaphoreGive(filesystem_mutex);
        ESP_LOGW(log_tag, "opendir failed path=%s errno=%d", path, open_error);
        return ESP_FAIL;
    }
    new_directory->media_generation = media_generation;
    directory = new_directory;
    return ESP_OK;
}

esp_err_t hal_storage_read_directory(
    hal_storage_directory* directory,
    hal_storage_entry& entry,
    bool& end_reached)
{
    end_reached = false;
    if (directory == nullptr || directory->stream == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (directory->media_generation != hal_get_storage_media_generation() ||
        hal_get_storage_state() != sd_state::ready) {
        return ESP_ERR_INVALID_STATE;
    }

    errno = 0;
    const dirent* item = readdir(directory->stream);
    if (item == nullptr) {
        end_reached = errno == 0;
        return end_reached ? ESP_OK : ESP_FAIL;
    }
    if (std::strlen(item->d_name) >= sizeof(entry.name)) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::strcpy(entry.name, item->d_name);

    char item_path[sizeof(directory->full_path) + SD_FILE_NAME_LENGTH + 1U];
    const int written = std::snprintf(
        item_path,
        sizeof(item_path),
        "%s%s%s",
        directory->full_path,
        directory->full_path[std::strlen(directory->full_path) - 1U] == '/' ? "" : "/",
        item->d_name);
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(item_path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    struct stat item_stat = {};
    if (stat(item_path, &item_stat) != 0) {
        return ESP_FAIL;
    }
    entry.directory = S_ISDIR(item_stat.st_mode);
    entry.size = entry.directory ? 0U : static_cast<std::uint64_t>(item_stat.st_size);
    return ESP_OK;
}

void hal_storage_close_directory(hal_storage_directory*& directory)
{
    if (directory == nullptr) {
        return;
    }
    if (directory->stream != nullptr) {
        closedir(directory->stream);
    }
    delete directory;
    directory = nullptr;
    xSemaphoreGive(filesystem_mutex);
}
