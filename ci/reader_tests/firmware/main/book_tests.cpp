// Compile production codecs/engine; emulate only the bounded SD I/O boundary.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <cJSON.h>

#include "book_format.hpp"
#include "book_index_engine.hpp"
#include "status_bar_layout.hpp"

namespace {
unsigned checks = 0U;
unsigned json_allocations = 0U;
#define VERIFY(c) do { ++checks; if (!(c)) { std::printf("TEST_FAILURE book line=%d: %s\n", __LINE__, #c); std::fflush(stdout); std::abort(); } } while (0)

constexpr char text_path[] = "/Books/example.txt";
constexpr char meta_path[] = "/.system/books/test/metadata.json";
constexpr char idx_path[] = "/.system/books/test/pages.idx";
std::uint16_t measure(std::uint32_t cp) { return cp < 128U ? 1U : 2U; }
const text_layout_profile layout{20U, 3U, measure};

struct memory_sd {
    std::map<std::string, std::string> files;
    std::vector<book_service_event> events;
    std::uint32_t generation = 1U;
    std::int64_t mtime = 100;
    unsigned reads = 0U, text_reads = 0U, sample_reads = 0U, writes = 0U, replacements = 0U;
    unsigned fail_write = 0U, fail_replace = 0U;
    bool fail_mkdir = false, fail_crc = false, removed = false, swap_before_replace = false;

    static bool valid(void* p, std::uint32_t generation)
    {
        auto& sd = *static_cast<memory_sd*>(p);
        return !sd.removed && generation == sd.generation;
    }
    static esp_err_t read(void* p, std::uint32_t generation, const char* path, std::uint64_t offset,
                          void* data, std::size_t capacity, std::size_t& length, std::uint64_t& size, std::int64_t& mtime)
    {
        auto& sd = *static_cast<memory_sd*>(p);
        if (!valid(p, generation)) { return ESP_ERR_INVALID_STATE; }
        VERIFY(capacity <= BOOK_SCAN_BUFFER_SIZE);
        ++sd.reads;
        if (std::strcmp(path, text_path) == 0) {
            ++sd.text_reads;
            if (capacity == 4096U) { ++sd.sample_reads; if (sd.fail_crc) { return ESP_FAIL; } }
        }
        const auto found = sd.files.find(path);
        if (found == sd.files.end()) { return ESP_ERR_NOT_FOUND; }
        size = found->second.size(); mtime = sd.mtime;
        if (offset > size) { return ESP_ERR_INVALID_SIZE; }
        length = std::min<std::size_t>(capacity, size - offset);
        std::memcpy(data, found->second.data() + offset, length);
        return ESP_OK;
    }
    static esp_err_t write(void* p, std::uint32_t generation, const char* path, std::uint64_t offset,
                           const void* data, std::size_t length, bool truncate)
    {
        auto& sd = *static_cast<memory_sd*>(p);
        if (!valid(p, generation)) { return ESP_ERR_INVALID_STATE; }
        VERIFY(std::strncmp(path, "/.system/", 9U) == 0 && length <= 4096U);
        if (++sd.writes == sd.fail_write) { return ESP_FAIL; }
        auto& file = sd.files[path];
        if (truncate) { file.clear(); }
        file.resize(std::max<std::size_t>(file.size(), offset + length));
        std::memcpy(file.data() + offset, data, length);
        return ESP_OK;
    }
    static esp_err_t mkdir(void* p, std::uint32_t generation, const char*)
    {
        return valid(p, generation) && !static_cast<memory_sd*>(p)->fail_mkdir ? ESP_OK : ESP_FAIL;
    }
    static esp_err_t replace(void* p, std::uint32_t generation, const char* from, const char* to)
    {
        auto& sd = *static_cast<memory_sd*>(p);
        if (sd.swap_before_replace) { ++sd.generation; sd.swap_before_replace = false; }
        if (!valid(p, generation)) { return ESP_ERR_INVALID_STATE; }
        if (++sd.replacements == sd.fail_replace) { return ESP_FAIL; }
        const auto found = sd.files.find(from);
        if (found == sd.files.end()) { return ESP_ERR_NOT_FOUND; }
        sd.files[to] = std::move(found->second); sd.files.erase(found);
        return ESP_OK;
    }
    static void emit(void* p, const book_service_event& event)
    {
        static_cast<memory_sd*>(p)->events.push_back(event);
    }
    book_engine_io io() { return {this, read, write, mkdir, replace, valid, emit}; }
    void reset_counts() { reads = text_reads = sample_reads = writes = replacements = 0U; events.clear(); }
};

void run(book_index_engine& engine)
{
    unsigned steps = 0U;
    while (engine.working() && steps++ < 100000U) { engine.step(); }
    VERIFY(!engine.working());
}

book_metadata load(const memory_sd& sd)
{
    book_metadata metadata = {};
    const auto& json = sd.files.at(meta_path);
    VERIFY(book_metadata_decode(json.data(), json.size(), metadata));
    return metadata;
}

void store(memory_sd& sd, const book_metadata& metadata)
{
    char json[BOOK_METADATA_CAPACITY] = {};
    VERIFY(book_metadata_encode(metadata, json, sizeof(json)));
    sd.files[meta_path] = json;
}

void verify_foreground_offsets(const memory_sd& sd)
{
    const auto& text = sd.files.at(text_path);
    const auto& index = sd.files.at(idx_path);
    reader_paginator foreground;
    std::uint64_t offset = 0U;
    std::uint32_t page = 0U;
    for (;;) {
        VERIFY(48U + std::uint64_t(page + 1U) * 8U <= index.size());
        VERIFY(book_decode_u64(reinterpret_cast<const std::uint8_t*>(index.data() + 48U + page * 8U)) == offset);
        foreground.reset(offset, layout);
        reader_parse_status status = reader_parse_status::need_data;
        while (status == reader_parse_status::need_data) {
            const auto at = foreground.read_offset();
            const auto length = std::min<std::size_t>(2048U, text.size() - at);
            status = foreground.feed(text.data() + at, length, at + length == text.size());
        }
        VERIFY(status == reader_parse_status::page_ready);
        ++page;
        if (foreground.page().end_of_file) { break; }
        offset = foreground.page().next_page_start_offset;
    }
    VERIFY(index.size() == 48U + std::uint64_t(page) * 8U);
}

void test_metadata()
{
    book_metadata value = {};
    std::strcpy(value.canonical_path, "/Books/中文\"title.txt");
    value.file = {10000U, 123, {1U, 2U, 3U}};
    value.index_format_version = BOOK_PAGE_INDEX_FORMAT_VERSION;
    value.pagination_version = BOOK_PAGINATION_VERSION;
    value.index_complete = true; value.page_count = 12U;
    value.progress = {3U, 300U}; value.offsets_crc32 = 42U;
    char json[BOOK_METADATA_CAPACITY] = {};
    VERIFY(book_metadata_encode(value, json, sizeof(json)));
    book_metadata decoded = {};
    VERIFY(book_metadata_decode(json, std::strlen(json), decoded));
    VERIFY(std::strcmp(value.canonical_path, decoded.canonical_path) == 0 && decoded.progress.byte_offset == 300U);
    for (unsigned mutation = 0U; mutation < 13U; ++mutation) {
        cJSON* root = cJSON_Parse(json);
        auto* file = cJSON_GetObjectItemCaseSensitive(root, "file");
        auto* progress = cJSON_GetObjectItemCaseSensitive(root, "progress");
        if (mutation == 0U) { cJSON_DeleteItemFromObjectCaseSensitive(root, "schema_version"); }
        if (mutation == 1U || mutation == 2U) {
            cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "schema_version"), mutation == 1U ? 0U : 2U);
        }
        if (mutation == 3U) { cJSON_DeleteItemFromObjectCaseSensitive(file, "mtime"); }
        if (mutation == 4U) { cJSON_ReplaceItemInObjectCaseSensitive(file, "size", cJSON_CreateString("10000")); }
        if (mutation == 5U) { cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(file, "size"), 1e30); }
        if (mutation == 6U) { cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(file, "size"), -1); }
        if (mutation == 7U) { cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(file, "size"), 12.5); }
        if (mutation == 8U) { cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(progress, "page"), 12); }
        if (mutation == 9U) { cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(progress, "byte_offset"), 10000); }
        if (mutation == 10U) { cJSON_AddNumberToObject(root, "schema_version", 1); }
        if (mutation == 11U) { cJSON_ReplaceItemInObjectCaseSensitive(root, "canonical_path", cJSON_CreateString("/Books/../bad.txt")); }
        if (mutation == 12U) { cJSON_ReplaceItemInObjectCaseSensitive(file, "fingerprint", cJSON_CreateNull()); }
        char* bad = cJSON_PrintUnformatted(root);
        VERIFY(!book_metadata_decode(bad, std::strlen(bad), decoded));
        cJSON_free(bad); cJSON_Delete(root);
    }
    for (const auto& bad : {std::string("{"), std::string(json) + "garbage", std::string(4096U, ' '),
                           std::string(40U, '[') + std::string(40U, ']')}) {
        VERIFY(!book_metadata_decode(bad.data(), bad.size(), decoded));
    }
    std::string wide_json = "{\"extra\":[0";
    for (unsigned i = 0U; i < 160U; ++i) { wide_json += ",0"; }
    wide_json += "]}";
    cJSON_Hooks counting = {[](std::size_t size) -> void* { ++json_allocations; return std::malloc(size); }, std::free};
    json_allocations = 0U;
    cJSON_InitHooks(&counting);
    VERIFY(!book_metadata_decode(wide_json.data(), wide_json.size(), decoded));
    VERIFY(json_allocations == 0U); // Reject wide arrays before allocating hundreds of nodes.
    cJSON_InitHooks(nullptr);
    cJSON_Hooks hooks = {[](std::size_t) -> void* { return nullptr; }, std::free};
    cJSON_InitHooks(&hooks);
    VERIFY(!book_metadata_encode(value, json, sizeof(json)));
    VERIFY(!book_metadata_decode("{}", 2U, decoded));
    cJSON_InitHooks(nullptr);
    std::puts("PASS metadata: valid/escaped UTF-8, schema/missing/type/range/duplicate/depth/JSON errors, allocation failure");
}

void test_header_and_fingerprint()
{
    std::uint8_t bytes[48] = {};
    book_index_header header{4U, 100U, 123U}, decoded = {};
    book_index_encode(header, bytes);
    VERIFY(book_index_decode(bytes, sizeof(bytes), 80U, 100U, decoded));
    for (unsigned at : {0U, 8U, 12U, 16U, 20U, 24U, 32U, 36U, 40U}) {
        bytes[at] ^= 1U;
        VERIFY(!book_index_decode(bytes, sizeof(bytes), 80U, 100U, decoded));
        bytes[at] ^= 1U;
    }
    VERIFY(!book_index_decode(bytes, 47U, 80U, 100U, decoded));
    VERIFY(!book_index_decode(bytes, 48U, 79U, 100U, decoded));
    VERIFY(!book_index_decode(bytes, 48U, 80U, 101U, decoded));
    VERIFY(book_index_offset_valid(0U, 0U, 0U, 0U));
    VERIFY(!book_index_offset_valid(1U, 0U, 0U, 100U));
    VERIFY(!book_index_offset_valid(20U, 20U, 2U, 100U));
    VERIFY(!book_index_offset_valid(101U, 20U, 2U, 100U));
    VERIFY(!book_index_offset_valid(100U, 20U, 2U, 100U));
    VERIFY(book_index_offset_valid(21U, 20U, 2U, 100U));
    VERIFY(book_crc32("123456789", 9U) == 0xcbf43926U);
    VERIFY(book_crc32("56789", 5U, book_crc32("1234", 4U)) == 0xcbf43926U);
    struct row { std::uint64_t size, middle, tail, length; };
    for (const row example : {row{0, 0, 0, 0}, row{123, 0, 0, 123}, row{4096, 0, 0, 4096},
                             row{6000, 952, 1904, 4096}, row{20000, 7952, 15904, 4096}}) {
        VERIFY(book_fingerprint_window(example.size, 0U).offset == 0U);
        VERIFY(book_fingerprint_window(example.size, 1U).offset == example.middle);
        VERIFY(book_fingerprint_window(example.size, 2U).offset == example.tail);
        VERIFY(book_fingerprint_window(example.size, 1U).length == example.length);
    }
    std::puts("PASS index/fingerprint: explicit header, corruption/truncation/bounds, CRC32 known vector, fixed sample windows");
}

void test_engine()
{
    memory_sd baseline;
    baseline.files[text_path] = std::string(20000U, 'a');
    auto builder = std::make_unique<book_index_engine>(baseline.io());
    builder->open(text_path, "test", layout, 1U, 1U);
    VERIFY(builder->working() && baseline.files.count(idx_path) == 0U);
    VERIFY(baseline.events.back().type == book_event_type::opened && !baseline.events.back().index_valid);
    // A duplicate attaches to the same scan; no second temporary file is started.
    const auto initial_writes = baseline.writes;
    builder->open(text_path, "test", layout, 2U, 1U);
    VERIFY(baseline.writes == initial_writes);
    builder->save(2U, 1234U); // Back while indexing, then continue without a Reader.
    VERIFY(!load(baseline).index_complete && load(baseline).progress.byte_offset == 1234U);
    run(*builder);
    VERIFY(baseline.events.back().index_valid);
    VERIFY(load(baseline).page_count == 334U && load(baseline).progress.byte_offset == 1200U);
    const auto writes = baseline.writes;
    builder->query(2U, 10U, true, 50U, 0U);
    VERIFY(baseline.events.back().progress.byte_offset == 3000U && baseline.events.back().progress.page == 50U);
    builder->query(2U, 11U, false, 0U, 3070U);
    VERIFY(baseline.events.back().progress.byte_offset == 3060U && baseline.events.back().progress.page == 51U);
    VERIFY(baseline.writes == writes);
    builder->save(2U, 3060U);
    VERIFY(load(baseline).progress.page == 51U);
    builder.reset(); // New worker-local state; restore from serialized SD files only.
    baseline.reset_counts();
    builder = std::make_unique<book_index_engine>(baseline.io());
    builder->open(text_path, "test", layout, 3U, 1U); run(*builder);
    VERIFY(baseline.events.back().index_valid && baseline.events.back().progress.page == 51U);
    VERIFY(baseline.text_reads == 2U && baseline.sample_reads == 0U && baseline.writes == 0U);
    builder.reset();
    const auto good_files = baseline.files;
    verify_foreground_offsets(baseline);

    {
        // Real old header and valid CRC, unchanged TXT/stat/fingerprint, but a
        // larger new page layout. Neither old page 51 nor byte 3070 may resume.
        memory_sd sd; sd.files = good_files;
        auto metadata = load(sd);
        const auto fingerprint = metadata.file.fingerprint;
        metadata.pagination_version = 1U;
        metadata.progress = {51U, 3070U}; store(sd, metadata);
        auto* bytes = reinterpret_cast<std::uint8_t*>(sd.files[idx_path].data());
        bytes[12] = 1U;
        const auto crc = book_crc32(bytes, 36U);
        for (unsigned i = 0; i < 4; ++i) { bytes[36U + i] = crc >> (i * 8U); }
        auto taller = layout; taller.line_count = 4U;
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", taller, 1U, 1U);
        VERIFY(sd.events.back().type == book_event_type::opened && engine->working());
        VERIFY(sd.events.back().progress.page == 0U && sd.events.back().progress.byte_offset == 0U);
        VERIFY(sd.sample_reads == 3U); // Before scanning: exactly the three fingerprint windows.
        run(*engine);
        VERIFY(sd.events.back().index_valid && sd.events.back().page_count == 250U);
        VERIFY(sd.events.back().progress.page == 0U && sd.events.back().progress.byte_offset == 0U);
        const auto rebuilt = load(sd);
        VERIFY(rebuilt.pagination_version == 2U && rebuilt.page_count == 250U);
        VERIFY(rebuilt.progress.page == 0U && rebuilt.progress.byte_offset == 0U);
        VERIFY(book_fingerprint_equal(fingerprint, rebuilt.file.fingerprint));
        VERIFY(rebuilt.file.modified_time == metadata.file.modified_time && rebuilt.file.file_size == metadata.file.file_size);
        VERIFY(sd.sample_reads > 3U && sd.replacements == 2U); // Scanning also uses 4 KiB reads.
        std::puts("PASS pagination v1 -> v2: unchanged TXT/stat/CRC, 334 -> 250 pages, old page/byte discarded, reopen at zero");
        engine.reset();
        engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", taller, 2U, 1U); run(*engine);
        VERIFY(sd.events.back().progress.page == 0U && sd.events.back().progress.byte_offset == 0U);
    }

    {
        memory_sd sd; sd.files = good_files;
        auto metadata = load(sd); std::strcpy(metadata.canonical_path, "/Books/different.txt"); store(sd, metadata);
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U);
        VERIFY(sd.events.back().type == book_event_type::error && sd.writes == 0U);
    }
    {
        memory_sd sd; sd.files = good_files;
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U);
        sd.files[idx_path][0] ^= 1; // Damage after initial header read, before streaming validation.
        run(*engine);
        VERIFY(sd.events.back().index_valid && sd.replacements == 2U);
    }

    // Reuse/update mtime, rebuild for file size, sample changes and pagination version.
    for (unsigned mutation = 0U; mutation < 9U; ++mutation) {
        memory_sd sd; sd.files = good_files;
        if (mutation == 0U) { sd.files.erase(idx_path); }
        if (mutation == 1U) { sd.files[text_path] += 'x'; }
        if (mutation >= 2U && mutation <= 5U) { ++sd.mtime; }
        if (mutation == 3U) { sd.files[text_path][0U] = 'b'; }
        if (mutation == 4U) { sd.files[text_path][10000U] = 'b'; }
        if (mutation == 5U) { sd.files[text_path][19999U] = 'b'; }
        if (mutation == 6U) {
            auto metadata = load(sd); metadata.pagination_version = 1U;
            metadata.progress = {51U, 3070U}; store(sd, metadata);
        }
        if (mutation == 7U) { sd.files[meta_path] = "{bad json}"; }
        if (mutation == 8U) {
            auto metadata = load(sd); metadata.progress = {3U, 3060U}; store(sd, metadata);
        }
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U);
        if (mutation == 6U) {
            VERIFY(sd.events.back().progress.page == 0U && sd.events.back().progress.byte_offset == 0U);
            VERIFY(!sd.events.back().index_valid && engine->working());
        }
        run(*engine);
        VERIFY(sd.events.back().index_valid);
        if (mutation == 2U) {
            VERIFY(sd.sample_reads == 3U && sd.replacements == 1U && sd.files[idx_path] == good_files.at(idx_path));
            VERIFY(load(sd).file.modified_time == 101);
        } else if (mutation == 8U) { VERIFY(sd.events.back().progress.page == 3U && sd.events.back().progress.byte_offset == 180U); }
        else {
            VERIFY(sd.writes > 2U && sd.replacements == 2U);
            if (mutation == 6U) { VERIFY(sd.events.back().progress.page == 0U && sd.events.back().progress.byte_offset == 0U); }
        }
    }

    for (unsigned mutation = 0U; mutation < 9U; ++mutation) {
        memory_sd sd; sd.files = good_files;
        auto& index = sd.files[idx_path];
        if (mutation == 0U) { index.resize(index.size() - 1U); }
        else if (mutation <= 4U) { index[mutation == 1U ? 0U : mutation == 2U ? 8U : mutation == 3U ? 12U : 20U] ^= 1; }
        else {
            auto* raw = reinterpret_cast<std::uint8_t*>(index.data());
            if (mutation == 5U) { book_encode_u64(raw + 48U + 8U, 0U); }
            if (mutation == 6U) { book_encode_u64(raw + 48U + 8U, 20001U); }
            if (mutation == 7U) { book_encode_u64(raw + 48U, 1U); }
            if (mutation == 8U) { raw[48U + 100U] ^= 1U; }
            if (mutation != 8U) {
                auto metadata = load(sd);
                metadata.offsets_crc32 = book_crc32(raw + 48U, index.size() - 48U);
                book_index_encode({metadata.page_count, metadata.file.file_size, metadata.offsets_crc32}, raw);
                store(sd, metadata); // Valid checksum must not hide invalid offset semantics.
            }
        }
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U); run(*engine);
        VERIFY(sd.events.back().index_valid && sd.replacements == 2U);
    }

    // Every supported failure stays recoverable; unfinished files never become valid.
    for (unsigned fault = 0U; fault < 9U; ++fault) {
        memory_sd sd; sd.files[text_path] = std::string(1000U, 'a');
        if (fault == 0U) { sd.fail_mkdir = true; }
        if (fault == 1U) { sd.fail_write = 1U; }
        if (fault == 2U) { sd.fail_write = 2U; }
        if (fault == 3U) { sd.fail_replace = 1U; }
        if (fault == 4U) { sd.fail_replace = 2U; }
        if (fault == 5U) { sd.files.erase(text_path); }
        if (fault == 6U) { sd.files[text_path] = std::string(20000U, 'a'); sd.fail_crc = true; }
        if (fault == 7U) { sd.swap_before_replace = true; }
        if (fault == 8U) { sd.files[text_path] = "\xff\xfe"; }
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U); run(*engine);
        VERIFY(sd.events.back().type == book_event_type::error && !sd.events.back().index_valid);
        VERIFY(sd.files.count(meta_path) == 0U);
    }
    for (bool swap : {false, true}) {
        memory_sd sd; sd.files[text_path] = std::string(20000U, 'a');
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U); engine->step();
        if (swap) { ++sd.generation; } else { sd.removed = true; }
        const auto writes_before = sd.writes;
        run(*engine);
        VERIFY(sd.writes == writes_before && sd.files.count(idx_path) == 0U && sd.files.count(meta_path) == 0U);
    }
    for (const auto& contents : {std::string(), std::string("\xef\xbb\xbf"), std::string(4096U, 'a'),
                                std::string(6000U, 'a'), std::string("中\r\nEnglish\n"),
                                std::string(4095U, 'a') + "中\r\n" + std::string(2046U, 'x') + "😀end",
                                std::string(4095U, 'a') + "\r\n\n\n" + std::string(120U, 'x')}) {
        memory_sd sd; sd.files[text_path] = contents;
        auto engine = std::make_unique<book_index_engine>(sd.io());
        engine->open(text_path, "test", layout, 1U, 1U); run(*engine);
        VERIFY(sd.events.back().index_valid);
        verify_foreground_offsets(sd);
    }
    std::puts("PASS engine: incremental build, reopen/reuse, mtime+CRC, head/middle/tail edits, version fallback, offset queries");
    std::puts("PASS engine faults: corrupt indices, missing/malformed files, full/write/replace/CRC failures, UTF-8, duplicate build, close/SD swap");
}

void test_status()
{
    status_bar_view_state state = {};
    VERIFY(!make_status_bar_page_layout(state, 480).visible);
    state.center_kind = status_bar_center_kind::page;
    for (const auto& pair : {std::pair<unsigned, unsigned>{1, 1}, {12, 438}, {999999, 999999}}) {
        state.center_current_page = pair.first;
        state.center_total_pages = pair.second;
        const auto draw = make_status_bar_page_layout(state, 480);
        VERIFY(draw.visible && draw.slash_x == 240 && draw.current_right == 232 && draw.total_left == 248);
        VERIFY(std::stoul(draw.current) == pair.first && std::stoul(draw.total) == pair.second);
    }
    state.center_total_pages = 1000000U;
    VERIFY(!make_status_bar_page_layout(state, 480).visible &&
           state.center_total_pages == 1000000U);
    state.center_current_page = state.center_total_pages = 1000001U;
    VERIFY(!make_status_bar_page_layout(state, 480).visible);
    state.center_current_page = state.center_total_pages = 1U;
    for (const auto app : {ui_view_id::launcher, ui_view_id::books,
                           ui_view_id::menu, ui_view_id::file,
                           ui_view_id::reader}) {
        state.foreground_app = app;
        VERIFY(make_status_bar_page_layout(state, 480).visible);
    }
    state.center_kind = status_bar_center_kind::text;
    VERIFY(!make_status_bar_page_layout(state, 480).visible);
    std::puts("PASS status: generic page/text context, 1/1 12/438 999999/999999, invariant slash, overflow hidden without clamping");
}
}  // namespace

void test_book_formats_and_engine()
{
    test_metadata(); test_header_and_fingerprint(); test_engine(); test_status();
    std::printf("SIZES book_engine=%u scan=%u event=%u checks=%u\n", unsigned(sizeof(book_index_engine)),
                unsigned(BOOK_SCAN_BUFFER_SIZE), unsigned(sizeof(book_service_event)), checks);
}
