#include "storage.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <new>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

#include <M5Unified.h>
#include <driver/sdmmc_default_configs.h>
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>
#include <utility/M5IOE1_Class.hpp>

#include "internal_i2c.hpp"

struct hal_storage_directory {
    DIR* stream;
    char full_path[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)];
};

namespace {

constexpr char log_tag[] = "hal_storage";
constexpr std::uint32_t filesystem_lock_timeout_ms = 1000U;
constexpr std::uint32_t power_settle_time_ms = 10U;

SemaphoreHandle_t filesystem_mutex = nullptr;
sdmmc_card_t* mounted_card = nullptr;

bool build_full_path(
    const char* path,
    char* output,
    std::size_t output_size)
{
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    const int written = std::snprintf(output, output_size, "%s%s", SD_MOUNT_POINT, path);
    return written >= 0 && static_cast<std::size_t>(written) < output_size;
}

}  // namespace

bool hal_storage_init()
{
    filesystem_mutex = xSemaphoreCreateMutex();
    if (filesystem_mutex == nullptr) {
        return false;
    }

    internal_i2c_guard bus_guard(INTERNAL_I2C_STORAGE_TIMEOUT_MS);
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
        ESP_LOGW(log_tag, "failed to configure TF_EN or TF_DET");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(power_settle_time_ms));
    return true;
}

bool hal_storage_card_inserted(bool& inserted)
{
    internal_i2c_guard bus_guard(INTERNAL_I2C_STORAGE_TIMEOUT_MS);
    if (!bus_guard.locked()) {
        return false;
    }

    bool detect_level = true;
    if (!M5.getIOExpander(0).getInputLevel(
            m5::M5IOE1_Class::gpio1,
            &detect_level)) {
        return false;
    }
    inserted = !detect_level;
    return true;
}

esp_err_t hal_storage_mount()
{
    if (mounted_card != nullptr) {
        return ESP_OK;
    }
    if (xSemaphoreTake(
            filesystem_mutex,
            pdMS_TO_TICKS(filesystem_lock_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

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

esp_err_t hal_storage_unmount()
{
    if (mounted_card == nullptr) {
        return ESP_OK;
    }
    if (xSemaphoreTake(
            filesystem_mutex,
            pdMS_TO_TICKS(filesystem_lock_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t result = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, mounted_card);
    if (result == ESP_OK || result == ESP_ERR_INVALID_STATE) {
        mounted_card = nullptr;
    }
    xSemaphoreGive(filesystem_mutex);
    return result == ESP_ERR_INVALID_STATE ? ESP_OK : result;
}

esp_err_t hal_storage_open_directory(
    const char* path,
    hal_storage_directory*& directory)
{
    directory = nullptr;
    if (mounted_card == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(
            filesystem_mutex,
            pdMS_TO_TICKS(filesystem_lock_timeout_ms)) != pdTRUE) {
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

esp_err_t hal_storage_read_file_chunk(
    const char* path, std::uint64_t offset, char* data, std::size_t capacity,
    std::size_t& length, std::uint64_t& file_size, std::int64_t& modified_time)
{
    length = 0U;
    file_size = 0U;
    modified_time = 0;
    if (data == nullptr || capacity == 0U ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return ESP_ERR_INVALID_ARG;
    }
    char full_path[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)] = {};
    if (!build_full_path(path, full_path, sizeof(full_path))) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(filesystem_mutex,
                       pdMS_TO_TICKS(filesystem_lock_timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    FILE* stream = nullptr;
    if (mounted_card == nullptr) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        stream = std::fopen(full_path, "rb");
        if (stream == nullptr) {
            result = errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        }
    }
    if (stream != nullptr) {
        struct stat metadata = {};
        if (fstat(fileno(stream), &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
            metadata.st_size < 0) {
            result = ESP_FAIL;
        } else {
            file_size = static_cast<std::uint64_t>(metadata.st_size);
            modified_time = static_cast<std::int64_t>(metadata.st_mtime);
            if (offset > file_size || fseeko(stream, static_cast<off_t>(offset), SEEK_SET) != 0) {
                result = ESP_ERR_INVALID_SIZE;
            } else {
                length = std::fread(data, 1U, capacity, stream);
                if (std::ferror(stream) != 0 || (length == 0U && offset < file_size)) {
                    result = ESP_FAIL;
                }
            }
        }
        if (std::fclose(stream) != 0) {
            result = ESP_FAIL;
        }
    }
    xSemaphoreGive(filesystem_mutex);
    return result;
}

namespace {
bool system_path(const char* path)
{
    return path != nullptr &&
        (std::strcmp(path, "/.system") == 0 || std::strncmp(path, "/.system/", 9U) == 0) &&
        std::strstr(path, "/../") == nullptr && std::strstr(path, "/./") == nullptr;
}

class filesystem_guard {
public:
    filesystem_guard()
        : locked_(xSemaphoreTake(filesystem_mutex, pdMS_TO_TICKS(filesystem_lock_timeout_ms)) == pdTRUE) {}
    ~filesystem_guard() { if (locked_) { xSemaphoreGive(filesystem_mutex); } }
    bool ready() const { return locked_ && mounted_card != nullptr; }
private:
    bool locked_;
};
}  // namespace

esp_err_t hal_storage_ensure_system_directory(const char* path)
{
    char full_path[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)] = {};
    if (!system_path(path) || !build_full_path(path, full_path, sizeof(full_path))) { return ESP_ERR_INVALID_ARG; }
    filesystem_guard guard;
    if (!guard.ready()) { return ESP_ERR_INVALID_STATE; }
    if (mkdir(full_path, 0755) == 0) { return ESP_OK; }
    struct stat info = {};
    return errno == EEXIST && stat(full_path, &info) == 0 && S_ISDIR(info.st_mode) ? ESP_OK : ESP_FAIL;
}

esp_err_t hal_storage_write_system_file(
    const char* path, std::uint64_t offset, const void* data, std::size_t length, bool truncate)
{
    char full_path[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)] = {};
    if (!system_path(path) || !build_full_path(path, full_path, sizeof(full_path)) || data == nullptr ||
        length > 4096U || offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - offset) {
        return ESP_ERR_INVALID_ARG;
    }
    filesystem_guard guard;
    if (!guard.ready()) { return ESP_ERR_INVALID_STATE; }
    FILE* stream = std::fopen(full_path, truncate ? "wb" : "r+b");
    if (stream == nullptr) { return ESP_FAIL; }
    esp_err_t result = ESP_OK;
    if (fseeko(stream, static_cast<off_t>(offset), SEEK_SET) != 0 ||
        std::fwrite(data, 1U, length, stream) != length || std::fflush(stream) != 0 ||
        fsync(fileno(stream)) != 0) { result = ESP_FAIL; }
    if (std::fclose(stream) != 0) { result = ESP_FAIL; }
    return result;
}

esp_err_t hal_storage_replace_system_file(const char* temporary, const char* destination)
{
    char from[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)] = {};
    char to[SD_PATH_LENGTH + sizeof(SD_MOUNT_POINT)] = {};
    if (!system_path(temporary) || !system_path(destination) || std::strcmp(temporary, destination) == 0 ||
        !build_full_path(temporary, from, sizeof(from)) || !build_full_path(destination, to, sizeof(to))) {
        return ESP_ERR_INVALID_ARG;
    }
    filesystem_guard guard;
    if (!guard.ready()) { return ESP_ERR_INVALID_STATE; }
    struct stat info = {};
    if (stat(from, &info) != 0 || !S_ISREG(info.st_mode)) { return ESP_FAIL; }
    // FAT rename need not replace an existing target. A power loss in this gap
    // leaves a missing final file, which the Service treats as a cache miss.
    if (std::remove(to) != 0 && errno != ENOENT) { return ESP_FAIL; }
    return std::rename(from, to) == 0 ? ESP_OK : ESP_FAIL;
}
