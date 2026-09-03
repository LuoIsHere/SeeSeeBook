#pragma once

#include <esp_err.h>

#include "book_format.hpp"
#include "text_paginator.hpp"

// Synchronous, worker-local I/O boundary; injectable for SD fault tests.
struct book_engine_io {
    void* context;
    esp_err_t (*read)(void*, std::uint32_t, const char*, std::uint64_t, void*, std::size_t,
                      std::size_t&, std::uint64_t&, std::int64_t&);
    esp_err_t (*write)(void*, std::uint32_t, const char*, std::uint64_t, const void*, std::size_t, bool);
    esp_err_t (*mkdir)(void*, std::uint32_t, const char*);
    esp_err_t (*replace)(void*, std::uint32_t, const char*, const char*);
    bool (*media_valid)(void*, std::uint32_t);
    void (*emit)(void*, const book_service_event&);
};

class book_index_engine {
public:
    explicit book_index_engine(const book_engine_io& io) : io_(io) {}
    void open(const char* path, const char* book_id, const text_layout_profile& layout,
              std::uint32_t session, std::uint32_t generation);
    void query(std::uint32_t session, std::uint32_t request, bool by_page,
               std::uint32_t page, std::uint64_t offset);
    void save(std::uint32_t session, std::uint64_t offset);
    void step();
    bool working() const;
    bool matches(const char* path, std::uint32_t generation) const;

private:
    enum class phase { idle, scanning, validating_cache, validating_new, ready, failed };
    book_engine_io io_;
    book_metadata metadata_ = {};
    book_progress resume_ = {}, position_ = {};
    book_index_header header_ = {};
    reader_paginator paginator_;
    text_layout_profile layout_ = {};
    char directory_[96] = {}, metadata_path_[128] = {}, index_path_[128] = {}, temporary_index_[132] = {};
    char json_[BOOK_METADATA_CAPACITY] = {};
    std::uint8_t buffer_[BOOK_SCAN_BUFFER_SIZE] = {};
    std::uint8_t offsets_[512] = {};
    std::size_t buffered_offsets_ = 0U, buffer_length_ = 0U;
    std::uint64_t buffer_offset_ = 0U, written_offsets_ = 0U, previous_offset_ = 0U;
    std::uint32_t session_ = 0U, generation_ = 0U, checked_offsets_ = 0U, checked_crc_ = 0U;
    phase phase_ = phase::idle;
    bool persistent_ = false, metadata_dirty_ = false, resume_by_page_ = false;

    void emit(book_event_type type, const book_progress& progress, std::uint32_t request = 0U,
              esp_err_t error = ESP_OK);
    void fail(esp_err_t error);
    bool text_read(std::uint64_t offset, void* data, std::size_t capacity, std::size_t& length);
    bool fingerprint(book_fingerprint& output);
    bool prepare_directory();
    bool write_metadata();
    bool flush_offsets();
    bool append_offset(std::uint64_t offset);
    bool read_offset(std::uint32_t page, std::uint64_t& offset);
    bool locate(book_progress& progress, bool prefer_page);
    void scan_step();
    bool begin_rebuild(bool calculate_fingerprint);
    void validate_step();
    void complete_index(bool newly_built);
};
