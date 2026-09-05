#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "book_index_engine.hpp"
#include "epub_archive.hpp"
#include "epub_format.hpp"

class epub_cache_engine {
public:
    explicit epub_cache_engine(const book_engine_io& io);
    esp_err_t open(const char* path, const char* book_id, std::uint32_t generation);
    esp_err_t step();
    void cancel();
    bool working() const;
    bool ready() const;
    bool rebuilt() const { return rebuilt_; }
    const char* content_path() const { return content_path_; }
    const char* cover_path() const { return cover_path_; }
    const epub_cache_metadata& metadata() const { return metadata_; }
    esp_err_t save(std::uint64_t linear_offset);

private:
    enum class phase : std::uint8_t { idle, cover, spine, finalize, ready, failed };
    book_engine_io io_;
    epub_zip_archive archive_;
    epub_zip_stream stream_;
    epub_package package_;
    epub_cache_metadata metadata_ = {};
    std::optional<epub_xhtml_filter> filter_;
    char directory_[96] = {};
    char metadata_path_[128] = {};
    char content_path_[128] = {};
    char temporary_content_[132] = {};
    char map_path_[128] = {};
    char temporary_map_[132] = {};
    char cover_path_[128] = {};
    char temporary_cover_[132] = {};
    char json_[EPUB_CACHE_METADATA_CAPACITY] = {};
    std::uint8_t output_[4096] = {};
    std::uint8_t map_bytes_[EPUB_SPINE_MAP_HEADER_SIZE + EPUB_SPINE_ITEM_LIMIT * 8U] = {};
    std::uint64_t starts_[EPUB_SPINE_ITEM_LIMIT] = {};
    std::size_t output_used_ = 0U;
    std::uint64_t content_written_ = 0U;
    std::uint64_t cover_written_ = 0U;
    std::uint16_t spine_index_ = 0U;
    std::uint16_t starts_count_ = 0U;
    std::uint32_t generation_ = 0U;
    esp_err_t sink_error_ = ESP_OK;
    phase phase_ = phase::idle;
    bool rebuilt_ = false;
    bool output_started_ = false;
    bool cover_started_ = false;

    bool prepare_directory();
    bool source_identity(const char* path, book_file_identity& identity, bool fingerprint);
    bool validate_cached(const char* path);
    bool validate_file(const char* path, std::uint64_t expected_size);
    bool load_map();
    bool write_metadata();
    esp_err_t begin_rebuild(const char* path);
    esp_err_t extract_xml(const char* path, std::size_t limit,
                          epub_unique_array<std::uint8_t>& bytes, std::size_t& length);
    esp_err_t begin_cover();
    esp_err_t begin_spine();
    esp_err_t finish_spine();
    esp_err_t finalize();
    bool write_text(const char* data, std::size_t length);
    bool flush_text();
    static bool text_write(void* context, const char* data, std::size_t length);
    static bool spine_write(void* context, const std::uint8_t* data, std::size_t length);
    static bool cover_write(void* context, const std::uint8_t* data, std::size_t length);
};
