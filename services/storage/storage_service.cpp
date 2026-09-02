#include "storage_service.hpp"

#include "service_event_source.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <strings.h>
#include <variant>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "storage.hpp"
#include "storage_file_reader.hpp"
#include "system_config.hpp"
#include "system_tick_service.hpp"

namespace {

constexpr char log_tag[] = "service_storage";
constexpr std::uint32_t request_queue_length = 4U;
constexpr std::uint32_t result_queue_length = STORAGE_RESULT_POOL_SIZE;
constexpr std::uint32_t status_queue_length = 8U;
constexpr std::uint32_t worker_stack_size = 6144U;
constexpr std::uint32_t monitor_stack_size = 4096U;
constexpr UBaseType_t worker_priority = 4U;
constexpr UBaseType_t monitor_priority = 5U;
constexpr std::uint8_t stable_sample_count = 3U;
constexpr std::uint32_t pool_lock_timeout_ms = 20U;
constexpr std::uint32_t reference_count_mask = 0xffffU;
constexpr std::uint32_t cleanup_pending_flag = 1U << 16U;

enum class storage_operation : std::uint8_t { list_directory, read_file_chunk };

struct storage_request {
    std::uint32_t request_id;
    std::uint32_t session_id;
    std::uint32_t media_generation;
    char path[STORAGE_MAX_PATH_LENGTH + 1U];
    std::uint64_t offset;
    storage_operation operation;
};

static_assert(std::is_trivially_copyable_v<storage_request>);

struct result_slot {
    std::atomic_uint32_t generation{0U};
    std::atomic_uint32_t reference_state{0U};
    std::atomic_bool filling{false};
    std::variant<storage_directory_result, storage_file_chunk_result> result;
};

QueueHandle_t request_queue = nullptr;
QueueHandle_t result_queue = nullptr;
QueueHandle_t status_queue = nullptr;
SemaphoreHandle_t result_pool_mutex = nullptr;
TaskHandle_t monitor_task_handle = nullptr;
std::array<result_slot, STORAGE_RESULT_POOL_SIZE> result_pool;
std::atomic<storage_state> current_state{storage_state::no_card};
std::atomic_uint32_t media_generation{0U};
bool unmount_pending = false;

bool normalize_path(
    const char* input,
    char* output,
    std::size_t output_size)
{
    if (input == nullptr || input[0] != '/' || output_size < 2U) {
        return false;
    }
    std::size_t output_length = 0U;
    output[output_length++] = '/';
    const char* cursor = input + 1;
    while (*cursor != '\0') {
        while (*cursor == '/') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        const char* component_start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }
        const std::size_t component_length =
            static_cast<std::size_t>(cursor - component_start);
        if ((component_length == 1U && component_start[0] == '.') ||
            (component_length == 2U && component_start[0] == '.' &&
             component_start[1] == '.')) {
            return false;
        }
        const std::size_t separator_length = output_length > 1U ? 1U : 0U;
        if (output_length + separator_length + component_length >= output_size) {
            return false;
        }
        if (separator_length != 0U) {
            output[output_length++] = '/';
        }
        std::memcpy(output + output_length, component_start, component_length);
        output_length += component_length;
    }
    output[output_length] = '\0';
    return true;
}

bool file_entry_less(const file_entry& left, const file_entry& right)
{
    if (left.type != right.type) {
        return left.type == file_entry_type::directory;
    }
    const int folded = strcasecmp(left.name.c_str(), right.name.c_str());
    return folded != 0 ? folded < 0 : left.name < right.name;
}

bool lock_result_pool()
{
    return xSemaphoreTake(
               result_pool_mutex,
               pdMS_TO_TICKS(pool_lock_timeout_ms)) == pdTRUE;
}

result_handle acquire_result_slot()
{
    if (!lock_result_pool()) {
        return invalid_result_handle();
    }
    result_handle handle = invalid_result_handle();
    for (std::uint16_t index = 0U; index < result_pool.size(); ++index) {
        result_slot& slot = result_pool[index];
        std::uint32_t reference_state = slot.reference_state.load();
        if (reference_state == cleanup_pending_flag) {
            slot.result.emplace<storage_directory_result>();
            slot.filling.store(false);
            slot.reference_state.store(0U);
            reference_state = 0U;
        }
        if (reference_state != 0U || slot.filling.load()) {
            continue;
        }
        std::uint32_t generation = slot.generation.load() + 1U;
        if (generation == 0U) {
            generation = 1U;
        }
        slot.generation.store(generation);
        slot.result.emplace<storage_directory_result>();
        slot.filling.store(true);
        slot.reference_state.store(1U);
        handle = {index, generation};
        break;
    }
    xSemaphoreGive(result_pool_mutex);
    return handle;
}

result_slot* mutable_result_slot(const result_handle& handle)
{
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) {
        return nullptr;
    }
    result_slot& slot = result_pool[handle.index];
    return slot.generation.load() == handle.generation &&
                   (slot.reference_state.load() & reference_count_mask) > 0U
               ? &slot
               : nullptr;
}

bool finish_result(const result_handle& handle)
{
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) {
        return false;
    }
    result_slot& slot = result_pool[handle.index];
    const bool valid = slot.generation.load() == handle.generation &&
                       (slot.reference_state.load() & reference_count_mask) > 0U &&
                       slot.filling.load();
    if (valid) {
        slot.filling.store(false);
    }
    return valid;
}

void publish_status(storage_state state, esp_err_t error = ESP_OK)
{
    current_state.store(state);
    storage_status_event event = {};
    event.state = state;
    event.media_generation = media_generation.load();
    event.error = error;
    if (xQueueSend(status_queue, &event, 0) != pdTRUE) {
        storage_status_event discarded = {};
        xQueueReceive(status_queue, &discarded, 0);
        xQueueSend(status_queue, &event, 0);
    }
}

storage_result_code scan_directory(
    const storage_request& request,
    storage_directory_result& result)
{
    if (current_state.load() != storage_state::ready ||
        media_generation.load() != request.media_generation) {
        return storage_result_code::no_card;
    }

    hal_storage_directory* directory = nullptr;
    const esp_err_t open_result = hal_storage_open_directory(request.path, directory);
    if (open_result != ESP_OK) {
        return open_result == ESP_ERR_INVALID_STATE
                   ? storage_result_code::cancelled
                   : storage_result_code::io_error;
    }
    result.entries.reserve(STORAGE_INITIAL_ENTRY_CAPACITY);
    storage_result_code code = storage_result_code::ok;
    for (;;) {
        if (current_state.load() != storage_state::ready ||
            media_generation.load() != request.media_generation) {
            code = storage_result_code::cancelled;
            break;
        }
        hal_storage_entry raw_entry = {};
        bool end_reached = false;
        const esp_err_t read_result =
            hal_storage_read_directory(directory, raw_entry, end_reached);
        if (read_result != ESP_OK) {
            code = storage_result_code::io_error;
            break;
        }
        if (end_reached) {
            break;
        }
        if (std::strcmp(raw_entry.name, ".") == 0 ||
            std::strcmp(raw_entry.name, "..") == 0) {
            continue;
        }
        if (result.entries.size() >= STORAGE_MAX_DIRECTORY_ENTRIES) {
            code = storage_result_code::too_many_entries;
            break;
        }
        file_entry entry = {};
        entry.name = raw_entry.name;
        entry.type = raw_entry.directory ? file_entry_type::directory
                                         : file_entry_type::file;
        entry.size = raw_entry.size;
        result.entries.push_back(std::move(entry));
    }
    hal_storage_close_directory(directory);
    if (code == storage_result_code::ok) {
        std::sort(result.entries.begin(), result.entries.end(), file_entry_less);
    }
    return code;
}

void publish_result(result_handle handle)
{
    if (!finish_result(handle) ||
        xQueueSend(result_queue, &handle, 0) != pdTRUE) {
        storage_service_release_result(handle);
        ESP_LOGW(log_tag, "result queue unavailable; result released");
    }
}

void storage_worker_task(void*)
{
    for (;;) {
        storage_request request = {};
        if (xQueueReceive(request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        result_handle handle = acquire_result_slot();
        result_slot* slot = mutable_result_slot(handle);
        if (slot == nullptr) {
            ESP_LOGW(log_tag, "result pool exhausted");
            continue;
        }
        if (request.operation == storage_operation::read_file_chunk) {
            auto& result = slot->result.emplace<storage_file_chunk_result>();
            result.request_id = request.request_id;
            result.session_id = request.session_id;
            result.media_generation = request.media_generation;
            result.offset = request.offset;
            result.code = storage_read_file_chunk(request.path, result);
        } else {
            auto& result = std::get<storage_directory_result>(slot->result);
            result.request_id = request.request_id;
            result.session_id = request.session_id;
            result.media_generation = request.media_generation;
            result.path = request.path;
            result.code = scan_directory(request, result);
            ESP_LOGI(log_tag, "scan path=%s entries=%u result=%u", request.path,
                     static_cast<unsigned>(result.entries.size()),
                     static_cast<unsigned>(result.code));
        }
        publish_result(handle);
    }
}

void handle_card_change(bool inserted)
{
    media_generation.fetch_add(1U);
    if (!inserted) {
        publish_status(storage_state::no_card);
        const esp_err_t result = hal_storage_unmount();
        unmount_pending = result == ESP_ERR_TIMEOUT;
        return;
    }
    if (unmount_pending && hal_storage_unmount() != ESP_OK) {
        publish_status(storage_state::error, ESP_ERR_INVALID_STATE);
        return;
    }
    unmount_pending = false;
    publish_status(storage_state::mounting);
    const esp_err_t result = hal_storage_mount();
    publish_status(
        result == ESP_OK ? storage_state::ready : storage_state::error,
        result);
}

void storage_monitor_task(void*)
{
    bool stable_inserted = false;
    bool candidate_inserted = false;
    bool stable_known = false;
    std::uint8_t matching_samples = 0U;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (unmount_pending && hal_storage_unmount() == ESP_OK) {
            unmount_pending = false;
        }
        bool inserted = false;
        if (!hal_storage_card_inserted(inserted)) {
            matching_samples = 0U;
            continue;
        }
        if (matching_samples == 0U || candidate_inserted != inserted) {
            candidate_inserted = inserted;
            matching_samples = 1U;
        } else if (matching_samples < stable_sample_count) {
            ++matching_samples;
        }
        if (matching_samples < stable_sample_count ||
            (stable_known && stable_inserted == candidate_inserted)) {
            continue;
        }
        stable_inserted = candidate_inserted;
        stable_known = true;
        handle_card_change(stable_inserted);
    }
}

}  // namespace

esp_err_t storage_service_init()
{
    request_queue = xQueueCreate(request_queue_length, sizeof(storage_request));
    result_queue = xQueueCreate(result_queue_length, sizeof(result_handle));
    status_queue = xQueueCreate(status_queue_length, sizeof(storage_status_event));
    result_pool_mutex = xSemaphoreCreateMutex();
    if (request_queue == nullptr || result_queue == nullptr ||
        status_queue == nullptr || result_pool_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            storage_worker_task,
            "storage_worker",
            worker_stack_size,
            nullptr,
            worker_priority,
            nullptr) != pdPASS ||
        xTaskCreate(
            storage_monitor_task,
            "storage_monitor",
            monitor_stack_size,
            nullptr,
            monitor_priority,
            &monitor_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (!system_tick_service_register_task(
            monitor_task_handle,
            STORAGE_DETECT_PERIOD_MS)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(log_tag, "storage service started result_pool=%u", STORAGE_RESULT_POOL_SIZE);
    return ESP_OK;
}

storage_state storage_service_get_state()
{
    return current_state.load();
}

std::uint32_t storage_service_get_media_generation()
{
    return media_generation.load();
}

bool storage_service_try_get_status_event(storage_status_event& event)
{
    return status_queue != nullptr && xQueueReceive(status_queue, &event, 0) == pdTRUE;
}

bool storage_service_list_directory(
    const char* path,
    std::uint32_t request_id,
    std::uint32_t session_id)
{
    if (request_queue == nullptr || current_state.load() != storage_state::ready) {
        return false;
    }
    storage_request request = {};
    request.request_id = request_id;
    request.session_id = session_id;
    request.media_generation = media_generation.load();
    if (!normalize_path(path, request.path, sizeof(request.path))) {
        return false;
    }
    return xQueueSend(request_queue, &request, 0) == pdTRUE;
}

bool storage_service_try_get_result_event(storage_result_event& event)
{
    return result_queue != nullptr &&
           xQueueReceive(result_queue, &event.handle, 0) == pdTRUE;
}

bool storage_service_read_file_chunk(
    const char* path, std::uint64_t offset, std::uint32_t request_id,
    std::uint32_t session_id, std::uint32_t requested_generation)
{
    if (request_queue == nullptr || current_state.load() != storage_state::ready ||
        requested_generation != media_generation.load()) {
        return false;
    }
    storage_request request = {};
    request.operation = storage_operation::read_file_chunk;
    request.request_id = request_id;
    request.session_id = session_id;
    request.media_generation = requested_generation;
    request.offset = offset;
    if (!normalize_path(path, request.path, sizeof(request.path))) {
        return false;
    }
    return xQueueSend(request_queue, &request, 0) == pdTRUE;
}

bool storage_service_resolve_result(
    const result_handle& handle,
    const storage_directory_result*& result)
{
    result = nullptr;
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) {
        return false;
    }
    const result_slot& slot = result_pool[handle.index];
    // The caller already owns one reference, so the result remains stable while
    // the returned task-local pointer is being used.
    const bool valid = slot.generation.load() == handle.generation &&
                       (slot.reference_state.load() & reference_count_mask) > 0U &&
                       !slot.filling.load();
    if (valid) {
        result = std::get_if<storage_directory_result>(&slot.result);
    }
    return valid && result != nullptr;
}

bool storage_service_resolve_file_result(
    const result_handle& handle, const storage_file_chunk_result*& result)
{
    result = nullptr;
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) {
        return false;
    }
    const result_slot& slot = result_pool[handle.index];
    if (slot.generation.load() == handle.generation &&
        (slot.reference_state.load() & reference_count_mask) > 0U &&
        !slot.filling.load()) {
        result = std::get_if<storage_file_chunk_result>(&slot.result);
    }
    return result != nullptr;
}

bool storage_service_retain_result(const result_handle& handle)
{
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) {
        return false;
    }
    result_slot& slot = result_pool[handle.index];
    if (slot.generation.load() != handle.generation || slot.filling.load()) {
        return false;
    }
    std::uint32_t state = slot.reference_state.load();
    for (;;) {
        const std::uint32_t references = state & reference_count_mask;
        if ((state & cleanup_pending_flag) != 0U || references == 0U ||
            references >= reference_count_mask) {
            return false;
        }
        if (slot.reference_state.compare_exchange_weak(state, state + 1U)) {
            return true;
        }
    }
}

bool storage_service_release_result(result_handle& handle)
{
    if (!result_handle_is_valid(handle)) {
        handle = invalid_result_handle();
        return true;
    }
    const result_handle requested = handle;
    if (handle.index >= result_pool.size()) {
        handle = invalid_result_handle();
        ESP_LOGW(
            log_tag,
            "release rejected index=%u generation=%lu",
            static_cast<unsigned>(requested.index),
            static_cast<unsigned long>(requested.generation));
        return false;
    }
    result_slot& slot = result_pool[handle.index];
    if (slot.generation.load() != handle.generation) {
        handle = invalid_result_handle();
        ESP_LOGW(
            log_tag,
            "release rejected stale handle=%u:%lu",
            static_cast<unsigned>(requested.index),
            static_cast<unsigned long>(requested.generation));
        return false;
    }
    std::uint32_t state = slot.reference_state.load();
    for (;;) {
        const std::uint32_t references = state & reference_count_mask;
        if ((state & cleanup_pending_flag) != 0U || references == 0U) {
            handle = invalid_result_handle();
            ESP_LOGW(
                log_tag,
                "release rejected unowned handle=%u:%lu state=0x%lx",
                static_cast<unsigned>(requested.index),
                static_cast<unsigned long>(requested.generation),
                static_cast<unsigned long>(state));
            return false;
        }
        const std::uint32_t next_state =
            references == 1U ? cleanup_pending_flag : state - 1U;
        if (slot.reference_state.compare_exchange_weak(state, next_state)) {
            handle = invalid_result_handle();
            return true;
        }
    }
}
