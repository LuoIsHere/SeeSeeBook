#include "book_catalog.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "book_catalog_support.hpp"
#include "book_format.hpp"
#include "epub_cache_engine.hpp"
#include "storage.hpp"
#include "storage_book_access.hpp"
#include "storage_service.hpp"

namespace {

constexpr char log_tag[] = "book_catalog";
constexpr char catalog_path[] = "/.system/books/catalog_v1.bin";
constexpr char catalog_temporary_path[] = "/.system/books/catalog_v1.bin.tmp";
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
constexpr std::size_t catalog_capacity = BOOK_CATALOG_ITEM_LIMIT;
constexpr std::size_t directory_limit = 256U;
#else
// Host/QEMU and boards without PSRAM retain a bounded functional fallback.
constexpr std::size_t catalog_capacity = 8U;
constexpr std::size_t directory_limit = 8U;
#endif
constexpr std::uint32_t worker_stack_size = 12288U;
constexpr UBaseType_t worker_priority = 1U;

struct catalog_disk_header {
    char magic[8];
    std::uint32_t schema;
    std::uint32_t item_size;
    std::uint32_t item_count;
    std::uint32_t settings_bits;
    std::uint32_t payload_crc;
};

static_assert(std::is_trivially_copyable_v<catalog_disk_header>);

struct media_guard {
    explicit media_guard(std::uint32_t generation)
        : locked(storage_book_access_begin(generation)) {}
    ~media_guard() { if (locked) { storage_book_access_end(); } }
    bool locked;
};

book_catalog_item* published_items = nullptr;
book_catalog_item* scan_items = nullptr;
char (*scan_directories)[BOOK_PATH_CAPACITY] = nullptr;
epub_cache_engine* catalog_epub = nullptr;
SemaphoreHandle_t state_mutex = nullptr;
QueueHandle_t event_queue = nullptr;
TaskHandle_t worker_handle = nullptr;

book_catalog_snapshot published_snapshot = {};
std::size_t published_count = 0U;

std::atomic_bool desired_active{false};
std::atomic_bool worker_busy{false};
std::atomic_uint32_t desired_generation{0U};
std::atomic_uint32_t activation_serial{0U};
std::atomic_uint32_t acknowledged_pause_serial{0U};
std::atomic_uint32_t settings_serial{0U};
std::atomic_uint8_t desired_settings_bits{1U};

std::uint8_t settings_bits(const book_catalog_settings& settings)
{
    return (settings.auto_scan_txt ? 1U : 0U) |
           (settings.auto_scan_epub ? 2U : 0U);
}

book_catalog_settings settings_from_bits(std::uint8_t bits)
{
    return {(bits & 1U) != 0U, (bits & 2U) != 0U};
}

bool media_valid(void*, std::uint32_t generation)
{
    return storage_service_get_state() == storage_state::ready &&
           storage_service_get_media_generation() == generation;
}

esp_err_t catalog_read(
    void*, std::uint32_t generation, const char* path,
    std::uint64_t offset, void* data, std::size_t capacity,
    std::size_t& length, std::uint64_t& size, std::int64_t& modified_time)
{
    media_guard guard(generation);
    return guard.locked
               ? hal_storage_read_file_chunk(
                     path, offset, static_cast<char*>(data), capacity,
                     length, size, modified_time)
               : ESP_ERR_INVALID_STATE;
}

esp_err_t catalog_write(
    void*, std::uint32_t generation, const char* path,
    std::uint64_t offset, const void* data, std::size_t length, bool truncate)
{
    media_guard guard(generation);
    return guard.locked
               ? hal_storage_write_system_file(path, offset, data, length, truncate)
               : ESP_ERR_INVALID_STATE;
}

esp_err_t catalog_mkdir(void*, std::uint32_t generation, const char* path)
{
    media_guard guard(generation);
    return guard.locked ? hal_storage_ensure_system_directory(path)
                        : ESP_ERR_INVALID_STATE;
}

esp_err_t catalog_replace(
    void*, std::uint32_t generation, const char* from, const char* to)
{
    media_guard guard(generation);
    return guard.locked ? hal_storage_replace_system_file(from, to)
                        : ESP_ERR_INVALID_STATE;
}

void ignore_book_event(void*, const book_service_event&) {}

const book_engine_io engine_io = {
    nullptr,
    catalog_read,
    catalog_write,
    catalog_mkdir,
    catalog_replace,
    media_valid,
    ignore_book_event,
};

void post_revision()
{
    book_catalog_event event = {};
    if (state_mutex != nullptr && xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
        event.media_generation = published_snapshot.media_generation;
        event.revision = published_snapshot.revision;
        xSemaphoreGive(state_mutex);
    }
    if (event_queue != nullptr) {
        xQueueOverwrite(event_queue, &event);
    }
}

void update_state(
    std::uint32_t generation,
    book_catalog_state state,
    esp_err_t error = ESP_OK)
{
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) { return; }
    const bool changed = published_snapshot.media_generation != generation ||
                         published_snapshot.item_count != published_count ||
                         published_snapshot.state != state ||
                         published_snapshot.error != error;
    published_snapshot.media_generation = generation;
    published_snapshot.item_count = static_cast<std::uint16_t>(published_count);
    published_snapshot.state = state;
    published_snapshot.error = error;
    if (changed && ++published_snapshot.revision == 0U) { ++published_snapshot.revision; }
    xSemaphoreGive(state_mutex);
    if (changed) { post_revision(); }
}

bool context_valid(
    std::uint32_t generation,
    std::uint32_t activation,
    std::uint32_t settings)
{
    return desired_active.load() && desired_generation.load() == generation &&
           activation_serial.load() == activation && settings_serial.load() == settings &&
           media_valid(nullptr, generation);
}

bool item_valid(const book_catalog_item& item)
{
    if (std::memchr(item.path, '\0', sizeof(item.path)) == nullptr ||
        std::memchr(item.book_id, '\0', sizeof(item.book_id)) == nullptr ||
        std::memchr(item.preview, '\0', sizeof(item.preview)) == nullptr ||
        book_catalog_path_format(item.path) != item.format ||
        std::strlen(item.book_id) != 64U || item.cover_size > EPUB_COVER_SIZE_LIMIT) {
        return false;
    }
    char expected[65] = {};
    return book_make_id(item.path, expected) && std::strcmp(expected, item.book_id) == 0;
}

esp_err_t read_exact(
    std::uint32_t generation,
    const char* path,
    std::uint64_t offset,
    void* data,
    std::size_t length,
    std::uint64_t& file_size)
{
    std::size_t actual = 0U;
    std::int64_t modified_time = 0;
    const auto error = catalog_read(
        nullptr, generation, path, offset, data, length,
        actual, file_size, modified_time);
    return error == ESP_OK && actual == length ? ESP_OK
                                                : (error == ESP_OK ? ESP_ERR_INVALID_SIZE : error);
}

bool load_catalog(std::uint32_t generation, book_catalog_settings& settings)
{
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
        published_count = 0U;
        xSemaphoreGive(state_mutex);
    }
    catalog_disk_header header = {};
    std::uint64_t file_size = 0U;
    auto error = read_exact(
        generation, catalog_path, 0U, &header, sizeof(header), file_size);
    if (error == ESP_ERR_NOT_FOUND) { return false; }
    const std::uint64_t expected_size = sizeof(header) +
        std::uint64_t(header.item_count) * sizeof(book_catalog_item);
    if (error != ESP_OK || std::memcmp(header.magic, "SSBCAT1", 8U) != 0 ||
        header.schema != BOOK_CATALOG_SCHEMA_VERSION ||
        header.item_size != sizeof(book_catalog_item) ||
        header.item_count > catalog_capacity || file_size != expected_size ||
        (header.settings_bits & ~3U) != 0U) {
        return false;
    }
    std::uint32_t crc = 0U;
    for (std::size_t index = 0U; index < header.item_count; ++index) {
        error = read_exact(
            generation, catalog_path,
            sizeof(header) + index * sizeof(book_catalog_item),
            &published_items[index], sizeof(book_catalog_item), file_size);
        if (error != ESP_OK || !item_valid(published_items[index])) {
            return false;
        }
        crc = book_crc32(&published_items[index], sizeof(book_catalog_item), crc);
    }
    if (crc != header.payload_crc) {
        return false;
    }
    std::size_t loaded_count = header.item_count;
    book_catalog_sort_unique(published_items, loaded_count);
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) { return false; }
    published_count = loaded_count;
    xSemaphoreGive(state_mutex);
    settings = settings_from_bits(static_cast<std::uint8_t>(header.settings_bits));
    return true;
}

bool save_catalog(std::uint32_t generation, const book_catalog_settings& settings)
{
    if (!media_valid(nullptr, generation)) { return false; }
    catalog_disk_header header = {};
    std::memcpy(header.magic, "SSBCAT1", 8U);
    header.schema = BOOK_CATALOG_SCHEMA_VERSION;
    header.item_size = sizeof(book_catalog_item);
    header.item_count = static_cast<std::uint32_t>(published_count);
    header.settings_bits = settings_bits(settings);
    for (std::size_t index = 0U; index < published_count; ++index) {
        header.payload_crc = book_crc32(
            &published_items[index], sizeof(book_catalog_item), header.payload_crc);
    }
    if (catalog_mkdir(nullptr, generation, "/.system") != ESP_OK ||
        catalog_mkdir(nullptr, generation, "/.system/books") != ESP_OK ||
        catalog_write(nullptr, generation, catalog_temporary_path, 0U,
                      &header, sizeof(header), true) != ESP_OK) {
        return false;
    }
    for (std::size_t index = 0U; index < published_count; ++index) {
        if (catalog_write(
                nullptr, generation, catalog_temporary_path,
                sizeof(header) + index * sizeof(book_catalog_item),
                &published_items[index], sizeof(book_catalog_item), false) != ESP_OK) {
            return false;
        }
    }
    return catalog_replace(
               nullptr, generation, catalog_temporary_path, catalog_path) == ESP_OK;
}

bool list_directory(
    std::uint32_t generation,
    const char* path,
    std::size_t& directory_count,
    std::size_t& item_count,
    const book_catalog_settings& settings)
{
    media_guard guard(generation);
    if (!guard.locked) { return false; }
    hal_storage_directory* directory = nullptr;
    if (hal_storage_open_directory(path, directory) != ESP_OK) { return false; }
    bool success = true;
    for (;;) {
        hal_storage_entry entry = {};
        bool end = false;
        const auto error = hal_storage_read_directory(directory, entry, end);
        if (error != ESP_OK) { success = false; break; }
        if (end) { break; }
        char child[BOOK_PATH_CAPACITY] = {};
        if (!book_catalog_join_path(path, entry.name, child, sizeof(child)) ||
            book_catalog_path_excluded(child)) {
            continue;
        }
        if (entry.directory) {
            if (directory_count >= directory_limit) { success = false; break; }
            std::strcpy(scan_directories[directory_count++], child);
            continue;
        }
        book_file_format format = book_file_format::unknown;
        if (!book_catalog_path_selected(child, settings, format)) { continue; }
        if (item_count >= catalog_capacity) { success = false; break; }
        auto& item = scan_items[item_count++];
        item = {};
        std::strcpy(item.path, child);
        item.source_size = entry.size;
        item.format = format;
        item.preview_state = format == book_file_format::txt
                                 ? book_catalog_preview_state::none
                                 : book_catalog_preview_state::unavailable;
        item.cover_state = format == book_file_format::epub
                               ? book_catalog_cover_state::pending
                               : book_catalog_cover_state::none;
    }
    hal_storage_close_directory(directory);
    return success;
}

bool populate_item(
    std::uint32_t generation,
    book_catalog_item& item)
{
    char bytes[BOOK_CATALOG_TXT_READ_LIMIT] = {};
    const std::size_t capacity = item.format == book_file_format::txt
                                     ? sizeof(bytes)
                                     : 1U;
    std::size_t length = 0U;
    std::uint64_t size = 0U;
    std::int64_t modified_time = 0;
    const auto error = catalog_read(
        nullptr, generation, item.path, 0U, bytes, capacity,
        length, size, modified_time);
    if (error != ESP_OK || modified_time < 0) { return false; }
    item.source_size = size;
    item.modified_time = modified_time;
    if (!book_make_id(item.path, item.book_id)) { return false; }
    if (item.format == book_file_format::txt) {
        return book_catalog_make_txt_preview(
            reinterpret_cast<const std::uint8_t*>(bytes), length,
            length == size, item.preview, sizeof(item.preview),
            item.preview_state);
    }
    return true;
}

bool scan_catalog(
    std::uint32_t generation,
    std::uint32_t activation,
    std::uint32_t settings_revision,
    const book_catalog_settings& settings,
    std::size_t& result_count)
{
    std::memset(scan_items, 0, sizeof(book_catalog_item) * catalog_capacity);
    std::memset(scan_directories, 0, BOOK_PATH_CAPACITY * directory_limit);
    std::strcpy(scan_directories[0], "/");
    std::size_t directory_count = 1U;
    std::size_t directory_index = 0U;
    result_count = 0U;
    while (directory_index < directory_count) {
        if (!context_valid(generation, activation, settings_revision) ||
            !list_directory(generation, scan_directories[directory_index++],
                            directory_count, result_count, settings)) {
            return false;
        }
        vTaskDelay(1U);
    }
    book_catalog_sort_unique(scan_items, result_count);
    std::size_t output = 0U;
    for (std::size_t index = 0U; index < result_count; ++index) {
        if (!context_valid(generation, activation, settings_revision)) { return false; }
        if (!populate_item(generation, scan_items[index])) { continue; }
        if (output != index) { scan_items[output] = scan_items[index]; }
        ++output;
        vTaskDelay(1U);
    }
    result_count = output;
    return true;
}

void replace_catalog(
    std::uint32_t generation,
    std::size_t count,
    const book_catalog_settings& settings)
{
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) { return; }
    std::swap(published_items, scan_items);
    published_count = count;
    published_snapshot.media_generation = generation;
    published_snapshot.item_count = static_cast<std::uint16_t>(count);
    published_snapshot.settings = settings;
    published_snapshot.state = book_catalog_state::enriching;
    published_snapshot.error = ESP_OK;
    if (++published_snapshot.revision == 0U) { ++published_snapshot.revision; }
    xSemaphoreGive(state_mutex);
    post_revision();
}

void filter_published(const book_catalog_settings& settings)
{
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) { return; }
    std::size_t output = 0U;
    for (std::size_t index = 0U; index < published_count; ++index) {
        const bool keep = (published_items[index].format == book_file_format::txt && settings.auto_scan_txt) ||
                          (published_items[index].format == book_file_format::epub && settings.auto_scan_epub);
        if (keep) { published_items[output++] = published_items[index]; }
    }
    published_count = output;
    published_snapshot.item_count = static_cast<std::uint16_t>(output);
    published_snapshot.settings = settings;
    if (++published_snapshot.revision == 0U) { ++published_snapshot.revision; }
    xSemaphoreGive(state_mutex);
    post_revision();
}

bool enrich_epub(
    std::uint32_t generation,
    std::uint32_t activation,
    std::uint32_t settings_revision,
    std::size_t index)
{
    book_catalog_item item = {};
    bool exists = false;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) { return false; }
    if (index < published_count) {
        item = published_items[index];
        exists = true;
    }
    xSemaphoreGive(state_mutex);
    if (!exists || item.format != book_file_format::epub ||
        item.cover_state != book_catalog_cover_state::pending) {
        return true;
    }
    auto error = catalog_epub->open(item.path, item.book_id, generation);
    while (error == ESP_OK && catalog_epub->working() &&
           context_valid(generation, activation, settings_revision)) {
        error = catalog_epub->step();
        vTaskDelay(1U);
    }
    if (!context_valid(generation, activation, settings_revision)) {
        catalog_epub->cancel();
        return false;
    }
    item.cover_state = book_catalog_cover_state::unavailable;
    item.cover_encoding = book_cover_encoding::none;
    item.cover_size = 0U;
    if (error == ESP_OK && catalog_epub->ready() &&
        catalog_epub->metadata().cover_encoding != book_cover_encoding::none) {
        item.cover_state = book_catalog_cover_state::ready;
        item.cover_encoding = catalog_epub->metadata().cover_encoding;
        item.cover_size = static_cast<std::uint32_t>(catalog_epub->metadata().cover_size);
    }
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) { return false; }
    if (index < published_count && std::strcmp(published_items[index].path, item.path) == 0) {
        published_items[index] = item;
        if (++published_snapshot.revision == 0U) { ++published_snapshot.revision; }
    }
    xSemaphoreGive(state_mutex);
    post_revision();
    return true;
}

void worker_task(void*)
{
    std::uint32_t current_generation = UINT32_MAX;
    std::uint32_t seen_activation = 0U;
    std::uint32_t seen_settings = 0U;
    book_catalog_settings settings = {true, false};
    bool loaded = false;
    bool scan_needed = false;
    bool enrich_needed = false;

    for (;;) {
        if (!desired_active.load()) {
            catalog_epub->cancel();
            worker_busy.store(false);
            acknowledged_pause_serial.store(activation_serial.load());
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
            continue;
        }
        const std::uint32_t generation = desired_generation.load();
        const std::uint32_t activation = activation_serial.load();
        const std::uint32_t setting_revision = settings_serial.load();

        if (activation != seen_activation) {
            bool unavailable = false;
            if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
                unavailable = published_snapshot.state == book_catalog_state::unavailable;
                xSemaphoreGive(state_mutex);
            }
            if (generation == current_generation && unavailable &&
                media_valid(nullptr, generation)) {
                loaded = false;
            }
            seen_activation = activation;
        }

        if (generation != current_generation) {
            catalog_epub->cancel();
            current_generation = generation;
            loaded = false;
            scan_needed = false;
            enrich_needed = false;
            if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
                published_count = 0U;
                xSemaphoreGive(state_mutex);
            }
        }
        if (!loaded) {
            worker_busy.store(true);
            update_state(generation, book_catalog_state::loading_cache);
            book_catalog_settings loaded_settings = {true, false};
            const bool have_cache = media_valid(nullptr, generation) &&
                                    load_catalog(generation, loaded_settings);
            settings = loaded_settings;
            desired_settings_bits.store(settings_bits(settings));
            seen_settings = setting_revision;
            if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
                published_snapshot.settings = settings;
                xSemaphoreGive(state_mutex);
            }
            update_state(
                generation,
                media_valid(nullptr, generation) ? book_catalog_state::ready
                                                 : book_catalog_state::unavailable,
                have_cache || !media_valid(nullptr, generation) ? ESP_OK : ESP_ERR_INVALID_CRC);
            loaded = true;
            scan_needed = media_valid(nullptr, generation) &&
                          (settings.auto_scan_txt || settings.auto_scan_epub);
        }
        if (setting_revision != seen_settings) {
            settings = settings_from_bits(desired_settings_bits.load());
            seen_settings = setting_revision;
            filter_published(settings);
            save_catalog(generation, settings);
            scan_needed = settings.auto_scan_txt || settings.auto_scan_epub;
            enrich_needed = false;
            if (!scan_needed) { update_state(generation, book_catalog_state::ready); }
            continue;
        }
        if (!media_valid(nullptr, generation)) {
            worker_busy.store(false);
            update_state(generation, book_catalog_state::unavailable);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
            continue;
        }
        if (scan_needed) {
            worker_busy.store(true);
            update_state(generation, book_catalog_state::scanning);
            std::size_t count = 0U;
            if (!scan_catalog(generation, activation, setting_revision, settings, count)) {
                if (context_valid(generation, activation, setting_revision)) {
                    update_state(generation, book_catalog_state::error, ESP_FAIL);
                    scan_needed = false;
                }
                continue;
            }
            replace_catalog(generation, count, settings);
            save_catalog(generation, settings);
            scan_needed = false;
            enrich_needed = true;
        }

        if (!enrich_needed) {
            worker_busy.store(false);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250U));
            continue;
        }
        bool pending_epub = false;
        std::size_t count = 0U;
        if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
            count = published_count;
            xSemaphoreGive(state_mutex);
        }
        for (std::size_t index = 0U; index < count; ++index) {
            book_catalog_item item = {};
            if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
                if (index < published_count) { item = published_items[index]; }
                xSemaphoreGive(state_mutex);
            }
            if (item.format != book_file_format::epub ||
                item.cover_state != book_catalog_cover_state::pending) {
                continue;
            }
            pending_epub = true;
            worker_busy.store(true);
            if (!enrich_epub(generation, activation, setting_revision, index)) { break; }
            pending_epub = false;
        }
        if (context_valid(generation, activation, setting_revision)) {
            save_catalog(generation, settings);
            update_state(generation, book_catalog_state::ready);
            enrich_needed = false;
        }
        worker_busy.store(false);
        if (!pending_epub) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250U)); }
    }
}

void* allocate_catalog_memory(std::size_t size)
{
#if defined(CONFIG_SPIRAM) && CONFIG_SPIRAM
    void* memory = heap_caps_calloc(1U, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory != nullptr) { return memory; }
#endif
    return heap_caps_calloc(1U, size, MALLOC_CAP_8BIT);
}

}  // namespace

esp_err_t book_catalog_service_init()
{
    published_items = static_cast<book_catalog_item*>(allocate_catalog_memory(
        sizeof(book_catalog_item) * catalog_capacity));
    scan_items = static_cast<book_catalog_item*>(allocate_catalog_memory(
        sizeof(book_catalog_item) * catalog_capacity));
    scan_directories = static_cast<char (*)[BOOK_PATH_CAPACITY]>(allocate_catalog_memory(
        BOOK_PATH_CAPACITY * directory_limit));
    void* epub_memory = allocate_catalog_memory(sizeof(epub_cache_engine));
    state_mutex = xSemaphoreCreateMutex();
    event_queue = xQueueCreate(1U, sizeof(book_catalog_event));
    if (published_items == nullptr || scan_items == nullptr || scan_directories == nullptr ||
        epub_memory == nullptr || state_mutex == nullptr || event_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    catalog_epub = new (epub_memory) epub_cache_engine(engine_io);
    published_snapshot.settings = {true, false};
    published_snapshot.state = book_catalog_state::unavailable;
    if (xTaskCreate(
            worker_task, "book_catalog", worker_stack_size, nullptr,
            worker_priority, &worker_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(log_tag, "catalog service started item_limit=%u item_bytes=%u",
             static_cast<unsigned>(catalog_capacity),
             static_cast<unsigned>(sizeof(book_catalog_item)));
    return ESP_OK;
}

bool book_catalog_service_activate(std::uint32_t media_generation)
{
    if (worker_handle == nullptr) { return false; }
    desired_generation.store(media_generation);
    activation_serial.fetch_add(1U);
    desired_active.store(true);
    xTaskNotifyGive(worker_handle);
    return true;
}

void book_catalog_service_pause()
{
    desired_active.store(false);
    activation_serial.fetch_add(1U);
    if (worker_handle != nullptr) { xTaskNotifyGive(worker_handle); }
}

bool book_catalog_service_idle()
{
    return !desired_active.load() && !worker_busy.load() &&
           acknowledged_pause_serial.load() == activation_serial.load();
}

bool book_catalog_service_update_settings(
    std::uint32_t media_generation,
    const book_catalog_settings& settings)
{
    if (worker_handle == nullptr || desired_generation.load() != media_generation) {
        return false;
    }
    desired_settings_bits.store(settings_bits(settings));
    settings_serial.fetch_add(1U);
    xTaskNotifyGive(worker_handle);
    return true;
}

bool book_catalog_service_snapshot(book_catalog_snapshot& snapshot)
{
    if (state_mutex == nullptr || xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    snapshot = published_snapshot;
    xSemaphoreGive(state_mutex);
    return true;
}

bool book_catalog_service_copy_page(
    std::uint32_t media_generation,
    std::size_t first_item,
    book_catalog_item* items,
    std::size_t capacity,
    std::size_t& copied)
{
    copied = 0U;
    if (items == nullptr || capacity == 0U || state_mutex == nullptr ||
        xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (published_snapshot.media_generation == media_generation && first_item < published_count) {
        copied = std::min(capacity, published_count - first_item);
        std::memcpy(items, published_items + first_item, copied * sizeof(book_catalog_item));
    }
    const bool valid = published_snapshot.media_generation == media_generation;
    xSemaphoreGive(state_mutex);
    return valid;
}

bool book_catalog_service_try_get_event(book_catalog_event& event)
{
    return event_queue != nullptr && xQueueReceive(event_queue, &event, 0U) == pdTRUE;
}

bool book_catalog_cover_path(
    const book_catalog_item& item,
    char* output,
    std::size_t capacity)
{
    if (output == nullptr || item.cover_state != book_catalog_cover_state::ready ||
        std::strlen(item.book_id) != 64U) {
        return false;
    }
    const int written = std::snprintf(
        output, capacity, "/.system/books/%s/epub_cover.bin", item.book_id);
    return written > 0 && static_cast<std::size_t>(written) < capacity;
}
