#include "reading_progress_service.hpp"

#include <array>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <mbedtls/sha256.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace {

constexpr char log_tag[] = "reading_progress";
constexpr std::uint32_t record_version = 1U;
constexpr std::size_t record_capacity = 16U;

struct progress_record {
    reading_file_identity identity;
    std::uint64_t offset;
    std::uint64_t sequence;
    std::uint32_t version;
    std::uint32_t reserved;
};

std::array<progress_record, record_capacity> records = {};
std::uint64_t sequence = 0U;
nvs_handle_t progress_nvs_handle = 0U;
bool initialized = false;
bool persistent = false;

bool same_path(const reading_file_identity& left, const reading_file_identity& right)
{
    // Full SHA-256 is kept in every record; there is no truncated hash key.
    // A cryptographic digest collision remains theoretically possible.
    return std::memcmp(left.path_digest, right.path_digest, sizeof(left.path_digest)) == 0;
}

void initialize()
{
    if (initialized) {
        return;
    }
    initialized = true;
    esp_err_t error = nvs_flash_init();
    if (error == ESP_OK) {
        error = nvs_open("reader_v1", NVS_READWRITE, &progress_nvs_handle);
    }
    if (error != ESP_OK) {
        ESP_LOGW(log_tag, "NVS unavailable: %s; progress stays in RAM", esp_err_to_name(error));
        return;
    }
    persistent = true;
    for (std::size_t index = 0U; index < records.size(); ++index) {
        char key[8] = {};
        std::snprintf(key, sizeof(key), "book%02u", static_cast<unsigned>(index));
        std::size_t length = sizeof(progress_record);
        auto& record = records[index];
        error = nvs_get_blob(progress_nvs_handle, key, &record, &length);
        if (error != ESP_OK || length != sizeof(record) || record.version != record_version) {
            record = {};
        } else if (record.sequence > sequence) {
            sequence = record.sequence;
        }
    }
}

}  // namespace

bool reading_progress_identify(const char* canonical_path, reading_file_identity& identity)
{
    identity = {};
    if (canonical_path == nullptr || canonical_path[0] != '/') {
        return false;
    }
    initialize();
    return mbedtls_sha256(
               reinterpret_cast<const unsigned char*>(canonical_path),
               std::strlen(canonical_path), identity.path_digest, 0) == 0;
}

bool reading_progress_load(const reading_file_identity& identity, std::uint64_t& offset)
{
    initialize();
    offset = 0U;
    for (const auto& record : records) {
        if (record.version == record_version && same_path(record.identity, identity) &&
            record.identity.file_size == identity.file_size &&
            record.identity.modified_time == identity.modified_time &&
            (record.offset < identity.file_size || record.offset == 0U)) {
            offset = record.offset;
            return true;
        }
    }
    return false;
}

esp_err_t reading_progress_save(const reading_file_identity& identity, std::uint64_t offset)
{
    initialize();
    if (offset != 0U && offset >= identity.file_size) {
        return ESP_ERR_INVALID_ARG;
    }
    std::size_t selected = 0U;
    for (std::size_t index = 0U; index < records.size(); ++index) {
        if (records[index].version == record_version && same_path(records[index].identity, identity)) {
            selected = index;
            break;
        }
        if (records[index].sequence < records[selected].sequence) {
            selected = index;
        }
    }
    auto& record = records[selected];
    // Skip an identical saved position; normal exits do not repeatedly write it.
    if (record.version == record_version && same_path(record.identity, identity) &&
        record.identity.file_size == identity.file_size &&
        record.identity.modified_time == identity.modified_time && record.offset == offset) {
        return persistent ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    record = {};
    record.identity = identity;
    record.offset = offset;
    record.sequence = ++sequence;
    record.version = record_version;
    if (!persistent) {
        return ESP_ERR_INVALID_STATE;
    }
    char key[8] = {};
    std::snprintf(key, sizeof(key), "book%02u", static_cast<unsigned>(selected));
    esp_err_t error = nvs_set_blob(progress_nvs_handle, key, &record, sizeof(record));
    if (error == ESP_OK) {
        error = nvs_commit(progress_nvs_handle);
    }
    if (error != ESP_OK) {
        persistent = false;
        ESP_LOGW(log_tag, "save failed: %s; RAM progress retained", esp_err_to_name(error));
    }
    return error;
}

bool reading_progress_is_persistent()
{
    initialize();
    return persistent;
}
