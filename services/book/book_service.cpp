#include "book_service.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <new>
#include <type_traits>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "book_index_engine.hpp"
#include "epub_cache_engine.hpp"
#include "service_event_source.hpp"
#include "storage.hpp"
#include "storage_book_access.hpp"
#include "storage_service.hpp"

namespace {
constexpr char log_tag[] = "book_service";
constexpr std::uint32_t worker_stack_size = 12288U;
constexpr UBaseType_t worker_priority = 2U;
constexpr std::size_t result_pool_size = 2U;

enum class operation : std::uint8_t { open, query, close, read };
struct command {
    operation type;
    char path[BOOK_PATH_CAPACITY];
    text_layout_profile layout;
    std::uint32_t session, generation, request, page;
    std::uint64_t offset;
    book_file_format format;
    book_content_kind content_kind;
    bool by_page;
};
static_assert(std::is_trivially_copyable_v<command>);

struct result_slot {
    std::atomic_uint32_t generation{0U};
    std::atomic_bool owned{false};
    book_content_result result = {};
};

QueueHandle_t commands = nullptr;
QueueHandle_t events = nullptr;
QueueHandle_t results = nullptr;
std::array<result_slot, result_pool_size> result_pool;

std::atomic<book_file_format> active_format{book_file_format::unknown};
char active_book_id[65] = {};
text_layout_profile active_layout = {};
std::uint32_t active_session = 0U;
std::uint32_t active_generation = 0U;
bool epub_index_started = false;

class media_guard {
public:
    explicit media_guard(std::uint32_t generation) : locked_(storage_book_access_begin(generation)) {}
    ~media_guard() { if (locked_) { storage_book_access_end(); } }
    bool locked() const { return locked_; }
private:
    bool locked_;
};

esp_err_t read(void*, std::uint32_t generation, const char* path, std::uint64_t offset,
               void* data, std::size_t capacity, std::size_t& length,
               std::uint64_t& size, std::int64_t& mtime)
{
    media_guard guard(generation);
    return guard.locked() ? hal_storage_read_file_chunk(path, offset, static_cast<char*>(data),
                                                         capacity, length, size, mtime)
                          : ESP_ERR_INVALID_STATE;
}

esp_err_t write(void*, std::uint32_t generation, const char* path, std::uint64_t offset,
                const void* data, std::size_t length, bool truncate)
{
    media_guard guard(generation);
    return guard.locked() ? hal_storage_write_system_file(path, offset, data, length, truncate)
                          : ESP_ERR_INVALID_STATE;
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

void publish_event(const book_service_event& value)
{
    if (value.error != ESP_OK) {
        ESP_LOGW(log_tag, "book operation failed session=%lu generation=%lu error=%s",
                 static_cast<unsigned long>(value.session_id),
                 static_cast<unsigned long>(value.media_generation),
                 esp_err_to_name(value.error));
    }
    if (xQueueSend(events, &value, pdMS_TO_TICKS(20U)) != pdTRUE) {
        book_service_event discarded = {};
        xQueueReceive(events, &discarded, 0U);
        xQueueSend(events, &value, 0U);
    }
}

void emit(void*, const book_service_event& source);
const book_engine_io io = {nullptr, read, write, mkdir, replace, media_valid, emit};
book_index_engine engine(io);
epub_cache_engine* epub_cache = nullptr;

void emit(void*, const book_service_event& source)
{
    book_service_event event = source;
    event.format = active_format.load();
    event.content_ready = true;
    if (active_format.load() == book_file_format::epub && epub_index_started && epub_cache != nullptr) {
        event.cover_available = epub_cache->metadata().cover_encoding != book_cover_encoding::none;
        event.persistent = event.persistent || epub_cache->ready();
    }
    publish_event(event);
}

result_handle acquire_result()
{
    for (std::uint16_t index = 0U; index < result_pool.size(); ++index) {
        bool expected = false;
        if (!result_pool[index].owned.compare_exchange_strong(expected, true)) { continue; }
        auto generation = result_pool[index].generation.fetch_add(1U) + 1U;
        if (generation == 0U) { generation = result_pool[index].generation.fetch_add(1U) + 1U; }
        result_pool[index].result = {};
        return {index, generation};
    }
    return invalid_result_handle();
}

void release_internal(result_handle handle)
{
    if (result_handle_is_valid(handle) && handle.index < result_pool.size() &&
        result_pool[handle.index].generation.load() == handle.generation) {
        result_pool[handle.index].owned.store(false);
    }
}

void publish_result(result_handle handle)
{
    if (xQueueSend(results, &handle, 0U) != pdTRUE) {
        release_internal(handle);
        ESP_LOGW(log_tag, "book result queue unavailable");
    }
}

bool make_book_id(const char* path, char* output)
{
    unsigned char digest[32] = {};
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(path), std::strlen(path), digest, 0) != 0) {
        return false;
    }
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0U; i < sizeof(digest); ++i) {
        output[2U * i] = digits[digest[i] >> 4U];
        output[2U * i + 1U] = digits[digest[i] & 15U];
    }
    output[64] = '\0';
    return true;
}

void emit_open_error(const command& request, esp_err_t error)
{
    book_service_event event = {};
    event.type = book_event_type::error;
    event.session_id = request.session;
    event.media_generation = request.generation;
    event.error = error;
    event.format = request.format;
    publish_event(event);
}

void start_epub_index()
{
    if (epub_cache == nullptr || !epub_cache->ready() || epub_index_started) { return; }
    epub_index_started = true;
    const book_progress fallback = {0U, epub_cache->metadata().progress.linear_offset};
    engine.open(epub_cache->content_path(), active_book_id, active_layout, active_session,
                active_generation, &fallback, epub_cache->rebuilt());
}

void open_book(const command& request)
{
    engine.cancel();
    if (epub_cache != nullptr) { epub_cache->cancel(); }
    active_format = request.format;
    active_session = request.session;
    active_generation = request.generation;
    active_layout = request.layout;
    epub_index_started = false;
    if (!make_book_id(request.path, active_book_id)) { emit_open_error(request, ESP_FAIL); return; }
    if (request.format == book_file_format::txt) {
        engine.open(request.path, active_book_id, request.layout, request.session, request.generation);
        return;
    }
    if (request.format != book_file_format::epub) { emit_open_error(request, ESP_ERR_NOT_SUPPORTED); return; }
    if (epub_cache == nullptr || results == nullptr) {
        emit_open_error(request, ESP_ERR_NO_MEM);
        return;
    }
    const auto error = epub_cache->open(request.path, active_book_id, request.generation);
    if (error != ESP_OK) { emit_open_error(request, error); return; }
    if (epub_cache->ready()) {
        start_epub_index();
    } else {
        book_service_event event = {};
        event.type = book_event_type::opened;
        event.session_id = request.session;
        event.media_generation = request.generation;
        event.format = book_file_format::epub;
        event.content_ready = false;
        publish_event(event);
    }
}

void read_content(const command& request)
{
    const result_handle handle = acquire_result();
    if (!result_handle_is_valid(handle)) { return; }
    auto& result = result_pool[handle.index].result;
    result.request_id = request.request;
    result.session_id = request.session;
    result.media_generation = request.generation;
    result.kind = request.content_kind;
    result.offset = request.offset;
    result.cover_encoding = epub_cache != nullptr && epub_cache->ready()
                                ? epub_cache->metadata().cover_encoding
                                : book_cover_encoding::none;
    if (request.format != book_file_format::epub || active_format.load() != book_file_format::epub ||
        request.session != active_session || request.generation != active_generation ||
        epub_cache == nullptr || !epub_cache->ready()) {
        result.error = ESP_ERR_INVALID_STATE;
        publish_result(handle);
        return;
    }
    const bool cover = request.content_kind == book_content_kind::cover;
    const char* path = cover ? epub_cache->cover_path() : epub_cache->content_path();
    const std::uint64_t expected = cover ? epub_cache->metadata().cover_size
                                         : epub_cache->metadata().content_size;
    if ((cover && epub_cache->metadata().cover_encoding == book_cover_encoding::none) ||
        request.offset > expected) {
        result.error = ESP_ERR_INVALID_ARG;
        publish_result(handle);
        return;
    }
    std::size_t length = 0U; std::uint64_t size = 0U; std::int64_t mtime = 0;
    result.error = read(nullptr, request.generation, path, request.offset, result.data,
                        sizeof(result.data), length, size, mtime);
    if (result.error == ESP_OK && (size != expected || length > UINT16_MAX ||
        request.offset > size || length > size - request.offset ||
        (length == 0U && request.offset < size))) {
        result.error = ESP_ERR_INVALID_SIZE;
    }
    result.file_size = size;
    result.length = static_cast<std::uint16_t>(length);
    result.end_of_file = result.error == ESP_OK && request.offset + length == size;
    publish_result(handle);
}

void worker(void*)
{
    for (;;) {
        const bool working = engine.working() || (epub_cache != nullptr && epub_cache->working());
        command request = {};
        if (xQueueReceive(commands, &request, working ? 0U : portMAX_DELAY) == pdTRUE &&
            media_valid(nullptr, request.generation)) {
            if (request.type == operation::open) {
                open_book(request);
            } else if (request.type == operation::query) {
                engine.query(request.session, request.request, request.by_page, request.page, request.offset);
            } else if (request.type == operation::read) {
                read_content(request);
            } else {
                if (active_format.load() == book_file_format::epub && epub_cache != nullptr &&
                    epub_cache->ready() &&
                    request.session == active_session) {
                    const auto save_error = epub_cache->save(request.offset);
                    if (save_error != ESP_OK) { emit_open_error(request, save_error); }
                }
                engine.save(request.session, request.offset);
            }
        }
        if (epub_cache != nullptr && epub_cache->working()) {
            const auto error = epub_cache->step();
            if (error != ESP_OK) {
                book_service_event event = {};
                event.type = book_event_type::error;
                event.session_id = active_session;
                event.media_generation = active_generation;
                event.format = book_file_format::epub;
                event.error = error;
                publish_event(event);
            } else if (epub_cache->ready()) {
                start_epub_index();
            }
        }
        engine.step();
        vTaskDelay(1U);
    }
}

bool submit(const command& request)
{
    if (commands == nullptr || !media_valid(nullptr, request.generation)) { return false; }
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
    results = xQueueCreate(result_pool_size, sizeof(result_handle));
    if (commands == nullptr || events == nullptr) { return ESP_ERR_NO_MEM; }
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void* epub_memory = heap_caps_malloc(sizeof(epub_cache_engine),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* epub_memory = heap_caps_malloc(sizeof(epub_cache_engine),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (epub_memory != nullptr) {
        epub_cache = new (epub_memory) epub_cache_engine(io);
    } else {
        ESP_LOGW(log_tag, "EPUB cache state allocation failed; TXT indexing remains available");
    }
    if (results == nullptr) {
        ESP_LOGW(log_tag, "EPUB result queue allocation failed; TXT indexing remains available");
    }
    if (xTaskCreate(worker, "book_worker", worker_stack_size, nullptr, worker_priority, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(log_tag, "worker: index=%u epub=%u stack=%lu scan=%u result_pool=%u",
             static_cast<unsigned>(sizeof(engine)), static_cast<unsigned>(sizeof(epub_cache_engine)),
             static_cast<unsigned long>(worker_stack_size), static_cast<unsigned>(BOOK_SCAN_BUFFER_SIZE),
             static_cast<unsigned>(result_pool_size));
    return ESP_OK;
}

bool book_service_open(const char* path, const text_layout_profile& layout,
                       std::uint32_t session, std::uint32_t media_generation,
                       book_file_format format)
{
    command request = {};
    request.type = operation::open;
    request.layout = layout;
    request.session = session;
    request.generation = media_generation;
    request.format = format;
    return book_canonical_path(path, request.path, sizeof(request.path)) && submit(request);
}

bool book_service_query(std::uint32_t session, std::uint32_t media_generation,
                        std::uint32_t request_id, bool by_page, std::uint32_t page,
                        std::uint64_t offset)
{
    command request = {};
    request.type = operation::query;
    request.session = session;
    request.generation = media_generation;
    request.request = request_id;
    request.by_page = by_page;
    request.page = page;
    request.offset = offset;
    request.format = active_format.load();
    return submit(request);
}

bool book_service_close(std::uint32_t session, std::uint32_t media_generation,
                        std::uint64_t offset)
{
    command request = {};
    request.type = operation::close;
    request.session = session;
    request.generation = media_generation;
    request.offset = offset;
    request.format = active_format.load();
    return submit(request);
}

bool book_service_read(std::uint32_t session, std::uint32_t media_generation,
                       std::uint32_t request_id, book_content_kind kind,
                       std::uint64_t offset)
{
    command request = {};
    request.type = operation::read;
    request.session = session;
    request.generation = media_generation;
    request.request = request_id;
    request.offset = offset;
    request.format = book_file_format::epub;
    request.content_kind = kind;
    return submit(request);
}

bool book_service_try_get_event(book_service_event& event)
{
    return events != nullptr && xQueueReceive(events, &event, 0U) == pdTRUE;
}

bool book_service_try_get_result_event(book_result_event& event)
{
    return results != nullptr && xQueueReceive(results, &event.handle, 0U) == pdTRUE;
}

bool book_service_resolve_result(const result_handle& handle,
                                 const book_content_result*& result)
{
    result = nullptr;
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) { return false; }
    const auto& slot = result_pool[handle.index];
    if (!slot.owned.load() || slot.generation.load() != handle.generation) { return false; }
    result = &slot.result;
    return true;
}

bool book_service_release_result(result_handle& handle)
{
    if (!result_handle_is_valid(handle) || handle.index >= result_pool.size()) {
        handle = invalid_result_handle(); return false;
    }
    const bool valid = result_pool[handle.index].owned.load() &&
                       result_pool[handle.index].generation.load() == handle.generation;
    if (valid) { result_pool[handle.index].owned.store(false); }
    handle = invalid_result_handle();
    return valid;
}
