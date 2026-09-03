#include "book_service.hpp"

#include <cstring>
#include <type_traits>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "book_index_engine.hpp"
#include "service_event_source.hpp"
#include "storage.hpp"
#include "storage_book_access.hpp"
#include "storage_service.hpp"

namespace {
constexpr char log_tag[] = "book_service";
constexpr std::uint32_t worker_stack_size = 8192U;
constexpr UBaseType_t worker_priority = 2U; // Storage foreground worker is priority 4.
enum class operation : std::uint8_t { open, query, close };
struct command {
    operation type;
    char path[BOOK_PATH_CAPACITY];
    // glyph_width is an immutable firmware function, never an App object pointer.
    text_layout_profile layout;
    std::uint32_t session, generation, request, page;
    std::uint64_t offset;
    bool by_page;
};
static_assert(std::is_trivially_copyable_v<command>);
QueueHandle_t commands = nullptr;
QueueHandle_t events = nullptr;

class media_guard {
public:
    explicit media_guard(std::uint32_t generation) : locked_(storage_book_access_begin(generation)) {}
    ~media_guard() { if (locked_) { storage_book_access_end(); } }
    bool locked() const { return locked_; }
private:
    bool locked_;
};

esp_err_t read(void*, std::uint32_t generation, const char* path, std::uint64_t offset,
                void* data, std::size_t capacity, std::size_t& length, std::uint64_t& size, std::int64_t& mtime)
{
    media_guard guard(generation);
    return guard.locked() ? hal_storage_read_file_chunk(path, offset, static_cast<char*>(data), capacity, length, size, mtime)
                          : ESP_ERR_INVALID_STATE;
}

esp_err_t write(void*, std::uint32_t generation, const char* path, std::uint64_t offset,
                 const void* data, std::size_t length, bool truncate)
{
    media_guard guard(generation);
    return guard.locked() ? hal_storage_write_system_file(path, offset, data, length, truncate) : ESP_ERR_INVALID_STATE;
}

esp_err_t mkdir(void*, std::uint32_t generation, const char* path)
{
    media_guard guard(generation);
    return guard.locked() ? hal_storage_ensure_system_directory(path) : ESP_ERR_INVALID_STATE;
}

esp_err_t replace(void*, std::uint32_t generation, const char* from, const char* to)
{
    media_guard guard(generation);
    return guard.locked() ? hal_storage_replace_system_file(from, to) : ESP_ERR_INVALID_STATE;
}

bool media_valid(void*, std::uint32_t generation)
{
    return storage_service_get_state() == storage_state::ready &&
           storage_service_get_media_generation() == generation;
}

void emit(void*, const book_service_event& event)
{
    if (event.error != ESP_OK) {
        ESP_LOGW(log_tag, "book operation failed session=%lu generation=%lu error=%s",
                 static_cast<unsigned long>(event.session_id), static_cast<unsigned long>(event.media_generation),
                 esp_err_to_name(event.error));
    }
    if (xQueueSend(events, &event, pdMS_TO_TICKS(20U)) != pdTRUE) {
        book_service_event discarded = {};
        xQueueReceive(events, &discarded, 0U);
        xQueueSend(events, &event, 0U);
    }
}

const book_engine_io io = {nullptr, read, write, mkdir, replace, media_valid, emit};
book_index_engine engine(io);

void worker(void*)
{
    for (;;) {
        command request = {};
        if (xQueueReceive(commands, &request, engine.working() ? 0U : portMAX_DELAY) == pdTRUE &&
            media_valid(nullptr, request.generation)) {
            if (request.type == operation::open) {
                unsigned char digest[32] = {};
                char book_id[65] = {};
                if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(request.path),
                                    std::strlen(request.path), digest, 0) != 0) {
                    book_service_event event = {};
                    event.type = book_event_type::error;
                    event.session_id = request.session;
                    event.media_generation = request.generation;
                    event.error = ESP_FAIL;
                    emit(nullptr, event);
                } else {
                    constexpr char digits[] = "0123456789abcdef";
                    for (std::size_t i = 0U; i < sizeof(digest); ++i) {
                        book_id[2U * i] = digits[digest[i] >> 4U];
                        book_id[2U * i + 1U] = digits[digest[i] & 15U];
                    }
                    engine.open(request.path, book_id, request.layout, request.session, request.generation);
                }
            } else if (request.type == operation::query) {
                engine.query(request.session, request.request, request.by_page, request.page, request.offset);
            } else {
                engine.save(request.session, request.offset);
            }
        }
        // Commands (including progress on Back) precede each bounded scan step.
        engine.step();
        vTaskDelay(1U);
    }
}

bool submit(const command& request)
{
    if (commands == nullptr || !media_valid(nullptr, request.generation)) { return false; }
    // All producers run on the App task. Reserve one slot for the matching
    // normal Back/save even when position queries saturate the control queue.
    if (request.type != operation::close && uxQueueSpacesAvailable(commands) <= 1U) { return false; }
    if (xQueueSend(commands, &request, 0U) == pdTRUE) { return true; }
    ESP_LOGW(log_tag, "book command queue full operation=%u", static_cast<unsigned>(request.type));
    return false;
}
}  // namespace

esp_err_t book_service_init()
{
    commands = xQueueCreate(8U, sizeof(command));
    events = xQueueCreate(8U, sizeof(book_service_event));
    if (commands == nullptr || events == nullptr) { return ESP_ERR_NO_MEM; }
    if (xTaskCreate(worker, "book_index", worker_stack_size, nullptr, worker_priority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(log_tag, "index worker: engine=%u stack=%lu scan=%u commands=%u events=%u",
             static_cast<unsigned>(sizeof(engine)), static_cast<unsigned long>(worker_stack_size),
             static_cast<unsigned>(BOOK_SCAN_BUFFER_SIZE), static_cast<unsigned>(8U * sizeof(command)),
             static_cast<unsigned>(8U * sizeof(book_service_event)));
    return ESP_OK;
}

bool book_service_open(const char* path, const text_layout_profile& layout,
                       std::uint32_t session, std::uint32_t media_generation)
{
    command request = {};
    request.type = operation::open;
    request.layout = layout;
    request.session = session;
    request.generation = media_generation;
    return book_canonical_path(path, request.path, sizeof(request.path)) && submit(request);
}

bool book_service_query(std::uint32_t session, std::uint32_t media_generation,
                        std::uint32_t request_id, bool by_page, std::uint32_t page, std::uint64_t offset)
{
    command request = {};
    request.type = operation::query;
    request.session = session;
    request.generation = media_generation;
    request.request = request_id;
    request.by_page = by_page;
    request.page = page;
    request.offset = offset;
    return submit(request);
}

bool book_service_close(std::uint32_t session, std::uint32_t media_generation, std::uint64_t offset)
{
    command request = {};
    request.type = operation::close;
    request.session = session;
    request.generation = media_generation;
    request.offset = offset;
    return submit(request);
}

bool book_service_try_get_event(book_service_event& event)
{
    return events != nullptr && xQueueReceive(events, &event, 0U) == pdTRUE;
}
