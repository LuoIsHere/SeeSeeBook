#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <esp_err.h>
#include <miniz.h>

#include "epub_format.hpp"
#include "epub_memory.hpp"

constexpr std::size_t EPUB_ZIP_ENTRY_LIMIT = 512U;
constexpr std::size_t EPUB_ZIP_IO_BUFFER_SIZE = 2048U;

struct epub_archive_io {
    void* context;
    esp_err_t (*read)(void* context, std::uint32_t generation, const char* path,
                      std::uint64_t offset, void* data, std::size_t capacity,
                      std::size_t& length, std::uint64_t& size, std::int64_t& mtime);
    bool (*media_valid)(void* context, std::uint32_t generation);
};

struct epub_zip_entry {
    char path[EPUB_ARCHIVE_PATH_CAPACITY];
    std::uint64_t local_header_offset;
    std::uint64_t compressed_size;
    std::uint64_t uncompressed_size;
    std::uint32_t crc32;
    std::uint16_t method;
    std::uint16_t flags;
};

class epub_zip_archive {
public:
    explicit epub_zip_archive(epub_archive_io io) : io_(io) {}
    ~epub_zip_archive() { close(); }
    epub_zip_archive(const epub_zip_archive&) = delete;
    epub_zip_archive& operator=(const epub_zip_archive&) = delete;
    esp_err_t open(const char* path, std::uint32_t generation);
    void close();
    const epub_zip_entry* find(const char* path) const;
    const char* path() const { return path_; }
    std::uint32_t generation() const { return generation_; }
    const book_file_identity& identity() const { return identity_; }
    const epub_archive_io& io() const { return io_; }
    std::uint64_t central_offset() const { return central_offset_; }

private:
    epub_archive_io io_;
    epub_unique_array<epub_zip_entry> entries_;
    std::size_t entry_count_ = 0U;
    char path_[BOOK_PATH_CAPACITY] = {};
    book_file_identity identity_ = {};
    std::uint64_t central_offset_ = 0U;
    std::uint32_t generation_ = 0U;
};

struct epub_stream_sink {
    void* context;
    bool (*write)(void* context, const std::uint8_t* data, std::size_t length);
};

class epub_zip_stream {
public:
    epub_zip_stream() = default;
    esp_err_t begin(const epub_zip_archive& archive, const epub_zip_entry& entry,
                    epub_stream_sink sink);
    esp_err_t step();
    bool done() const { return done_; }
    std::uint64_t output_size() const { return output_size_; }
    void reset();

private:
    const epub_zip_archive* archive_ = nullptr;
    epub_zip_entry entry_ = {};
    epub_stream_sink sink_ = {};
    epub_unique_array<std::uint8_t> dictionary_;
    epub_unique_array<tinfl_decompressor> decompressor_;
    std::uint8_t input_[EPUB_ZIP_IO_BUFFER_SIZE] = {};
    std::size_t input_offset_ = 0U;
    std::size_t input_length_ = 0U;
    std::size_t dictionary_offset_ = 0U;
    std::size_t pending_output_offset_ = 0U;
    std::size_t pending_output_length_ = 0U;
    std::uint64_t data_offset_ = 0U;
    std::uint64_t compressed_read_ = 0U;
    std::uint64_t compressed_consumed_ = 0U;
    std::uint64_t output_size_ = 0U;
    std::uint32_t crc32_ = 0U;
    bool done_ = false;
    bool decompression_done_ = false;

    esp_err_t fill_input();
    esp_err_t finish();
    esp_err_t deliver_pending();
};

esp_err_t epub_zip_extract_memory(const epub_zip_archive& archive,
                                  const epub_zip_entry& entry,
                                  std::uint8_t* output, std::size_t capacity);
