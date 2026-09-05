#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "book_format.hpp"
#include "epub_memory.hpp"

constexpr std::uint32_t EPUB_CACHE_SCHEMA_VERSION = 1U;
constexpr std::uint32_t EPUB_PARSER_VERSION = 1U;
constexpr std::size_t EPUB_ARCHIVE_PATH_CAPACITY = 257U;
constexpr std::size_t EPUB_CONTAINER_XML_LIMIT = 64U * 1024U;
constexpr std::size_t EPUB_PACKAGE_XML_LIMIT = 256U * 1024U;
constexpr std::uint64_t EPUB_SPINE_ITEM_SIZE_LIMIT = 8U * 1024U * 1024U;
constexpr std::uint64_t EPUB_CONTENT_SIZE_LIMIT = 64U * 1024U * 1024U;
constexpr std::uint64_t EPUB_COVER_SIZE_LIMIT = 512U * 1024U;
constexpr std::size_t EPUB_MANIFEST_ITEM_LIMIT = 128U;
constexpr std::size_t EPUB_CACHE_METADATA_CAPACITY = 4096U;
constexpr std::size_t EPUB_SPINE_MAP_HEADER_SIZE = 40U;

struct epub_spine_item {
    char path[EPUB_ARCHIVE_PATH_CAPACITY];
};

struct epub_package {
    epub_unique_array<epub_spine_item> spine;
    std::uint16_t spine_count = 0U;
    char cover_path[EPUB_ARCHIVE_PATH_CAPACITY];
    char cover_document_path[EPUB_ARCHIVE_PATH_CAPACITY];
    book_cover_encoding cover_encoding;
};

struct epub_cache_metadata {
    char canonical_path[BOOK_PATH_CAPACITY];
    book_file_identity source;
    std::uint64_t content_size;
    std::uint64_t cover_size;
    std::uint16_t spine_count;
    book_cover_encoding cover_encoding;
    epub_position progress;
    std::uint32_t parser_version;
    std::uint32_t pagination_version;
    bool complete;
};

bool epub_resolve_path(const char* base_file, const char* reference,
                       char* output, std::size_t capacity);
bool epub_normalize_archive_path(const char* source, char* output,
                                 std::size_t capacity);
bool epub_parse_container(const char* xml, std::size_t length,
                          char* rootfile, std::size_t capacity);
bool epub_parse_package(const char* xml, std::size_t length,
                        const char* package_path, epub_package& output);
bool epub_parse_cover_document(const char* xml, std::size_t length,
                               const char* document_path, char* image_path,
                               std::size_t capacity);
book_cover_encoding epub_cover_encoding_from_path(const char* path);

struct epub_text_sink {
    void* context;
    bool (*write)(void* context, const char* data, std::size_t length);
};

class epub_xhtml_filter {
public:
    explicit epub_xhtml_filter(epub_text_sink sink) : sink_(sink) {}
    bool feed(const std::uint8_t* data, std::size_t length, bool end);
    bool finish_chapter();
    std::uint64_t output_size() const { return output_size_; }

private:
    enum class mode : std::uint8_t { text, tag, entity, comment };
    epub_text_sink sink_;
    mode mode_ = mode::text;
    char token_[256] = {};
    std::size_t token_length_ = 0U;
    char utf8_[4] = {};
    std::uint8_t utf8_length_ = 0U;
    std::uint8_t comment_tail_ = 0U;
    char tag_quote_ = '\0';
    std::uint64_t output_size_ = 0U;
    std::uint16_t ignored_depth_ = 0U;
    bool token_overflow_ = false;
    bool pending_space_ = false;
    bool wrote_text_ = false;
    char last_output_ = '\0';

    bool emit(const char* data, std::size_t length);
    bool emit_codepoint(std::uint32_t codepoint);
    bool emit_newline(bool paragraph);
    bool finish_tag();
    bool finish_entity();
    bool text_byte(std::uint8_t byte);
};

bool epub_cache_metadata_decode(const char* json, std::size_t length,
                                epub_cache_metadata& output);
bool epub_cache_metadata_encode(const epub_cache_metadata& value,
                                char* json, std::size_t capacity);

void epub_spine_map_encode(const std::uint64_t* starts, std::uint16_t count,
                           std::uint64_t content_size, std::uint8_t* bytes,
                           std::size_t capacity, std::size_t& length);
bool epub_spine_map_decode(const std::uint8_t* bytes, std::size_t length,
                           std::uint64_t expected_content_size,
                           std::uint64_t* starts, std::size_t capacity,
                           std::uint16_t& count);
epub_position epub_position_from_linear(const std::uint64_t* starts,
                                        std::uint16_t count,
                                        std::uint64_t linear_offset);
