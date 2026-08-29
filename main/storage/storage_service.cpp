#include "storage_service.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <strings.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr char log_tag[] = "storage_service";

struct storage_request {
    std::uint32_t request_id;
    std::uint32_t session_id;
    std::uint32_t media_generation;
    char path[STORAGE_MAX_PATH_LENGTH + 1U];
};

static_assert(std::is_trivially_copyable_v<storage_request>);

QueueHandle_t request_queue = nullptr;
QueueHandle_t result_queue = nullptr;

bool normalize_path(const char* input, char* output, std::size_t output_size)
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
        const std::size_t component_length = static_cast<std::size_t>(cursor - component_start);
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
    const int folded_comparison = strcasecmp(left.name.c_str(), right.name.c_str());
    if (folded_comparison != 0) {
        return folded_comparison < 0;
    }
    return left.name < right.name;
}

storage_result_code scan_directory(
    const storage_request& request,
    storage_directory_result& result)
{
    if (hal_get_storage_state() != sd_state::ready ||
        hal_get_storage_media_generation() != request.media_generation) {
        return storage_result_code::no_card;
    }

    hal_storage_directory* directory = nullptr;
    const esp_err_t open_result = hal_storage_open_directory(
        request.path,
        request.media_generation,
        directory);
    if (open_result != ESP_OK) {
        return open_result == ESP_ERR_INVALID_STATE
                   ? storage_result_code::cancelled
                   : storage_result_code::io_error;
    }

    result.entries.reserve(STORAGE_INITIAL_ENTRY_CAPACITY);
    storage_result_code code = storage_result_code::ok;
    for (;;) {
        hal_storage_entry raw_entry = {};
        bool end_reached = false;
        const esp_err_t read_result = hal_storage_read_directory(
            directory,
            raw_entry,
            end_reached);
        if (read_result != ESP_OK) {
            code = read_result == ESP_ERR_INVALID_STATE
                       ? storage_result_code::cancelled
                       : storage_result_code::io_error;
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
        entry.type = raw_entry.directory ? file_entry_type::directory : file_entry_type::file;
        entry.size = raw_entry.size;
        result.entries.push_back(std::move(entry));
    }
    hal_storage_close_directory(directory);
    if (code == storage_result_code::ok) {
        std::sort(result.entries.begin(), result.entries.end(), file_entry_less);
    }
    return code;
}

void publish_result(storage_directory_result* result)
{
    storage_event event = {};
    event.type = storage_event_type::directory_result;
    event.directory_result = result;
    if (xQueueSend(result_queue, &event, 0) != pdTRUE) {
        delete result;
        ESP_LOGW(log_tag, "result queue full; directory result discarded");
    }
}

void storage_worker_task(void*)
{
    for (;;) {
        storage_request request = {};
        if (xQueueReceive(request_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        auto* result = new (std::nothrow) storage_directory_result{};
        if (result == nullptr) {
            ESP_LOGE(log_tag, "directory result allocation failed");
            continue;
        }
        result->request_id = request.request_id;
        result->session_id = request.session_id;
        result->media_generation = request.media_generation;
        result->path = request.path;
        result->code = scan_directory(request, *result);
        ESP_LOGI(
            log_tag,
            "scan complete path=%s entries=%u result=%u",
            request.path,
            static_cast<unsigned>(result->entries.size()),
            static_cast<unsigned>(result->code));
        publish_result(result);
    }
}

}  // namespace

esp_err_t storage_service_init()
{
    request_queue = xQueueCreate(STORAGE_REQUEST_QUEUE_LENGTH, sizeof(storage_request));
    result_queue = xQueueCreate(STORAGE_RESULT_QUEUE_LENGTH, sizeof(storage_event));
    if (request_queue == nullptr || result_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(
            storage_worker_task,
            "storage_worker",
            STORAGE_WORKER_TASK_STACK_SIZE,
            nullptr,
            STORAGE_WORKER_TASK_PRIORITY,
            nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(log_tag, "storage worker started");
    return ESP_OK;
}

bool storage_service_list_directory(
    const char* path,
    std::uint32_t request_id,
    std::uint32_t session_id)
{
    if (request_queue == nullptr || hal_get_storage_state() != sd_state::ready) {
        return false;
    }

    storage_request request = {};
    request.request_id = request_id;
    request.session_id = session_id;
    request.media_generation = hal_get_storage_media_generation();
    if (!normalize_path(path, request.path, sizeof(request.path))) {
        ESP_LOGW(log_tag, "invalid directory path rejected");
        return false;
    }
    if (xQueueSend(request_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(log_tag, "request queue full path=%s", request.path);
        return false;
    }
    ESP_LOGI(
        log_tag,
        "scan queued path=%s request=%lu session=%lu generation=%lu",
        request.path,
        static_cast<unsigned long>(request.request_id),
        static_cast<unsigned long>(request.session_id),
        static_cast<unsigned long>(request.media_generation));
    return true;
}

bool storage_service_try_get_event(storage_event& event)
{
    return result_queue != nullptr && xQueueReceive(result_queue, &event, 0) == pdTRUE;
}

void storage_service_release_event(storage_event& event)
{
    delete event.directory_result;
    event.directory_result = nullptr;
}

