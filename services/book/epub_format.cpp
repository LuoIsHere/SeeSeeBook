#include "epub_format.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <cJSON.h>

#include "utf8.hpp"

namespace {
constexpr std::uint8_t spine_magic[8] = {'S', 'S', 'B', 'S', 'P', 'N', '0', '1'};

struct xml_attribute {
    std::string_view name;
    std::string_view value;
};

std::string_view local_name(std::string_view name)
{
    const auto colon = name.find_last_of(':');
    return colon == std::string_view::npos ? name : name.substr(colon + 1U);
}

bool equal_folded(std::string_view left, std::string_view right)
{
    if (left.size() != right.size()) { return false; }
    for (std::size_t i = 0U; i < left.size(); ++i) {
        const auto a = static_cast<unsigned char>(left[i]);
        const auto b = static_cast<unsigned char>(right[i]);
        if (std::tolower(a) != std::tolower(b)) { return false; }
    }
    return true;
}

bool token_contains(std::string_view list, std::string_view wanted)
{
    for (std::size_t start = 0U; start < list.size();) {
        while (start < list.size() && std::isspace(static_cast<unsigned char>(list[start]))) { ++start; }
        std::size_t end = start;
        while (end < list.size() && !std::isspace(static_cast<unsigned char>(list[end]))) { ++end; }
        if (equal_folded(list.substr(start, end - start), wanted)) { return true; }
        start = end;
    }
    return false;
}

bool next_tag(const char* xml, std::size_t length, std::size_t& cursor,
              std::string_view& tag)
{
    for (;;) {
        while (cursor < length && xml[cursor] != '<') { ++cursor; }
        if (cursor == length) { return false; }
        if (length - cursor >= 4U && std::memcmp(xml + cursor, "<!--", 4U) == 0) {
            cursor += 4U;
            while (length - cursor >= 3U && std::memcmp(xml + cursor, "-->", 3U) != 0) { ++cursor; }
            if (length - cursor < 3U) { return false; }
            cursor += 3U;
            continue;
        }
        const std::size_t start = ++cursor;
        char quote = '\0';
        while (cursor < length) {
            const char value = xml[cursor++];
            if (quote != '\0') {
                if (value == quote) { quote = '\0'; }
            } else if (value == '\'' || value == '"') {
                quote = value;
            } else if (value == '>') {
                tag = std::string_view(xml + start, cursor - start - 1U);
                return true;
            }
        }
        return false;
    }
}

std::string_view tag_name(std::string_view tag, bool& closing)
{
    std::size_t cursor = 0U;
    while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) { ++cursor; }
    closing = cursor < tag.size() && tag[cursor] == '/';
    if (closing) { ++cursor; }
    const std::size_t start = cursor;
    while (cursor < tag.size() && !std::isspace(static_cast<unsigned char>(tag[cursor])) &&
           tag[cursor] != '/' && tag[cursor] != '>') { ++cursor; }
    return local_name(tag.substr(start, cursor - start));
}

bool attribute(std::string_view tag, std::string_view wanted, std::string_view& value)
{
    std::size_t cursor = 0U;
    while (cursor < tag.size() && !std::isspace(static_cast<unsigned char>(tag[cursor]))) { ++cursor; }
    while (cursor < tag.size()) {
        while (cursor < tag.size() && (std::isspace(static_cast<unsigned char>(tag[cursor])) ||
               tag[cursor] == '/')) { ++cursor; }
        const std::size_t name_start = cursor;
        while (cursor < tag.size() && !std::isspace(static_cast<unsigned char>(tag[cursor])) &&
               tag[cursor] != '=' && tag[cursor] != '/') { ++cursor; }
        const auto name = local_name(tag.substr(name_start, cursor - name_start));
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) { ++cursor; }
        if (cursor >= tag.size() || tag[cursor] != '=') {
            while (cursor < tag.size() && !std::isspace(static_cast<unsigned char>(tag[cursor]))) { ++cursor; }
            continue;
        }
        ++cursor;
        while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) { ++cursor; }
        if (cursor >= tag.size() || (tag[cursor] != '\'' && tag[cursor] != '"')) { return false; }
        const char quote = tag[cursor++];
        const std::size_t value_start = cursor;
        while (cursor < tag.size() && tag[cursor] != quote) { ++cursor; }
        if (cursor >= tag.size()) { return false; }
        if (equal_folded(name, wanted)) {
            value = tag.substr(value_start, cursor - value_start);
            return true;
        }
        ++cursor;
    }
    return false;
}

bool copy_xml_value(std::string_view source, char* destination, std::size_t capacity)
{
    if (capacity == 0U) { return false; }
    std::size_t used = 0U;
    for (std::size_t i = 0U; i < source.size();) {
        std::uint32_t codepoint = 0U;
        std::size_t consumed = 1U;
        if (source[i] == '&') {
            const auto end = source.find(';', i + 1U);
            if (end == std::string_view::npos || end - i > 12U) { return false; }
            const auto entity = source.substr(i + 1U, end - i - 1U);
            if (entity == "amp") { codepoint = '&'; }
            else if (entity == "lt") { codepoint = '<'; }
            else if (entity == "gt") { codepoint = '>'; }
            else if (entity == "quot") { codepoint = '"'; }
            else if (entity == "apos") { codepoint = '\''; }
            else { return false; }
            consumed = end - i + 1U;
        } else {
            std::size_t size = 0U;
            if (utf8_decode(source.data() + i, source.size() - i, codepoint, size) !=
                utf8_decode_result::complete) { return false; }
            consumed = size;
        }
        char encoded[5] = {};
        const auto size = utf8_encode(codepoint, encoded);
        if (used + size >= capacity) { return false; }
        std::memcpy(destination + used, encoded, size);
        used += size;
        i += consumed;
    }
    destination[used] = '\0';
    return used != 0U;
}

book_cover_encoding cover_type(std::string_view media, std::string_view path)
{
    if (equal_folded(media, "image/jpeg") || equal_folded(media, "image/jpg")) {
        return book_cover_encoding::jpeg;
    }
    if (equal_folded(media, "image/png")) { return book_cover_encoding::png; }
    const auto dot = path.find_last_of('.');
    if (dot != std::string_view::npos) {
        const auto extension = path.substr(dot);
        if (equal_folded(extension, ".jpg") || equal_folded(extension, ".jpeg")) {
            return book_cover_encoding::jpeg;
        }
        if (equal_folded(extension, ".png")) { return book_cover_encoding::png; }
    }
    return book_cover_encoding::none;
}

bool bounded_json(const char* text, std::size_t size)
{
    if (text == nullptr || size == 0U || size >= EPUB_CACHE_METADATA_CAPACITY) { return false; }
    unsigned depth = 0U, tokens = 0U;
    bool string = false, escape = false;
    for (std::size_t i = 0U; i < size; ++i) {
        const unsigned char c = text[i];
        if (c == 0U) { return false; }
        if (string) {
            if (escape) { escape = false; }
            else if (c == '\\') { escape = true; }
            else if (c == '"') { string = false; }
            else if (c < 0x20U) { return false; }
        } else if (c == '"') { string = true; ++tokens; }
        else if (c == '{' || c == '[') { if (++depth > 8U) { return false; } ++tokens; }
        else if (c == '}' || c == ']') { if (depth == 0U) { return false; } --depth; }
        else if (c == ',' || c == ':') { ++tokens; }
        if (tokens > 128U) { return false; }
    }
    return depth == 0U && !string;
}

bool unique_keys(const cJSON* object)
{
    if (!cJSON_IsObject(object)) { return false; }
    for (const cJSON* item = object->child; item != nullptr; item = item->next) {
        for (const cJSON* other = item->next; other != nullptr; other = other->next) {
            if (std::strcmp(item->string, other->string) == 0) { return false; }
        }
    }
    return true;
}

bool number(const cJSON* object, const char* key, std::uint64_t maximum, std::uint64_t& value)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > static_cast<double>(maximum) ||
        std::floor(item->valuedouble) != item->valuedouble) { return false; }
    value = static_cast<std::uint64_t>(item->valuedouble);
    return true;
}

void encode_u32(std::uint8_t* bytes, std::uint32_t value)
{
    for (unsigned i = 0U; i < 4U; ++i) { bytes[i] = static_cast<std::uint8_t>(value >> (8U * i)); }
}

std::uint32_t decode_u32(const std::uint8_t* bytes)
{
    std::uint32_t value = 0U;
    for (unsigned i = 0U; i < 4U; ++i) { value |= std::uint32_t(bytes[i]) << (8U * i); }
    return value;
}
}  // namespace

bool epub_resolve_path(const char* base_file, const char* reference,
                       char* output, std::size_t capacity)
{
    if (base_file == nullptr || reference == nullptr || output == nullptr || capacity < 2U) { return false; }
    char joined[EPUB_ARCHIVE_PATH_CAPACITY * 2U] = {};
    const char* end = reference;
    while (*end != '\0' && *end != '#' && *end != '?') { ++end; }
    if (end == reference || reference[0] == '/' || reference[0] == '\\') { return false; }
    const char* first_slash = std::find(reference, end, '/');
    const char* colon = std::find(reference, end, ':');
    if (colon != end && colon < first_slash) { return false; }
    const char* slash = std::strrchr(base_file, '/');
    const std::size_t prefix = slash == nullptr ? 0U : static_cast<std::size_t>(slash - base_file + 1U);
    if (prefix + static_cast<std::size_t>(end - reference) >= sizeof(joined)) { return false; }
    std::memcpy(joined, base_file, prefix);
    std::size_t joined_length = prefix;
    for (const char* cursor = reference; cursor < end;) {
        unsigned char value = static_cast<unsigned char>(*cursor++);
        if (value == '%') {
            if (end - cursor < 2) { return false; }
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') { return c - '0'; }
                if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
                if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
                return -1;
            };
            const int high = hex(cursor[0]), low = hex(cursor[1]);
            if (high < 0 || low < 0) { return false; }
            value = static_cast<unsigned char>((high << 4) | low);
            cursor += 2;
        }
        if (value == 0U || value < 0x20U || value == 0x7fU || value == '\\') { return false; }
        joined[joined_length++] = static_cast<char>(value);
    }
    joined[joined_length] = '\0';
    std::size_t used = 0U;
    for (std::size_t cursor = 0U; cursor < joined_length;) {
        while (cursor < joined_length && joined[cursor] == '/') { ++cursor; }
        const std::size_t start = cursor;
        while (cursor < joined_length && joined[cursor] != '/') { ++cursor; }
        const std::size_t size = cursor - start;
        if (size == 0U || (size == 1U && joined[start] == '.')) { continue; }
        if (size == 2U && joined[start] == '.' && joined[start + 1U] == '.') {
            if (used == 0U) { return false; }
            while (used > 0U && output[used - 1U] != '/') { --used; }
            if (used > 0U) { --used; }
            continue;
        }
        if (used != 0U) {
            if (used + 1U >= capacity) { return false; }
            output[used++] = '/';
        }
        if (used + size >= capacity) { return false; }
        std::memcpy(output + used, joined + start, size);
        used += size;
    }
    output[used] = '\0';
    for (std::size_t cursor = 0U; cursor < used;) {
        std::uint32_t codepoint = 0U; std::size_t size = 0U;
        if (utf8_decode(output + cursor, used - cursor, codepoint, size) != utf8_decode_result::complete) { return false; }
        cursor += size;
    }
    return used != 0U;
}

bool epub_normalize_archive_path(const char* source, char* output,
                                 std::size_t capacity)
{
    if (source == nullptr || output == nullptr || source[0] == '/' || source[0] == '\\' ||
        capacity < 2U) { return false; }
    std::size_t used = 0U;
    const std::size_t length = strnlen(source, EPUB_ARCHIVE_PATH_CAPACITY);
    if (length == 0U || length >= EPUB_ARCHIVE_PATH_CAPACITY) { return false; }
    for (std::size_t cursor = 0U; cursor < length;) {
        while (cursor < length && source[cursor] == '/') { ++cursor; }
        const std::size_t start = cursor;
        while (cursor < length && source[cursor] != '/') {
            const unsigned char value = static_cast<unsigned char>(source[cursor]);
            if (value == '\\' || value < 0x20U || value == 0x7fU) { return false; }
            ++cursor;
        }
        const std::size_t size = cursor - start;
        if (size == 0U || (size == 1U && source[start] == '.')) { continue; }
        if (size == 2U && source[start] == '.' && source[start + 1U] == '.') {
            if (used == 0U) { return false; }
            while (used > 0U && output[used - 1U] != '/') { --used; }
            if (used > 0U) { --used; }
            continue;
        }
        if (used != 0U) {
            if (used + 1U >= capacity) { return false; }
            output[used++] = '/';
        }
        if (used + size >= capacity) { return false; }
        std::memcpy(output + used, source + start, size);
        used += size;
    }
    output[used] = '\0';
    for (std::size_t cursor = 0U; cursor < used;) {
        std::uint32_t codepoint = 0U; std::size_t size = 0U;
        if (utf8_decode(output + cursor, used - cursor, codepoint, size) != utf8_decode_result::complete) { return false; }
        cursor += size;
    }
    return used != 0U;
}

bool epub_parse_container(const char* xml, std::size_t length,
                          char* rootfile, std::size_t capacity)
{
    if (xml == nullptr || length == 0U || length > EPUB_CONTAINER_XML_LIMIT) { return false; }
    std::size_t cursor = 0U;
    std::string_view tag;
    while (next_tag(xml, length, cursor, tag)) {
        bool closing = false;
        if (!equal_folded(tag_name(tag, closing), "rootfile") || closing) { continue; }
        std::string_view value;
        if (!attribute(tag, "full-path", value)) { return false; }
        char decoded[EPUB_ARCHIVE_PATH_CAPACITY] = {};
        if (!copy_xml_value(value, decoded, sizeof(decoded))) { return false; }
        return epub_resolve_path("", decoded, rootfile, capacity);
    }
    return false;
}

bool epub_parse_package(const char* xml, std::size_t length,
                        const char* package_path, epub_package& output)
{
    output = {};
    if (xml == nullptr || package_path == nullptr || length == 0U || length > EPUB_PACKAGE_XML_LIMIT) { return false; }
    struct manifest_item {
        char id[97];
        char path[EPUB_ARCHIVE_PATH_CAPACITY];
        char media[65];
        bool cover;
        bool document;
    };
    struct spine_ref { char id[97]; bool linear; };
    auto manifest = epub_allocate_array<manifest_item>(EPUB_MANIFEST_ITEM_LIMIT);
    auto spine = epub_allocate_array<spine_ref>(EPUB_SPINE_ITEM_LIMIT);
    if (!manifest || !spine) { return false; }
    std::size_t manifest_count = 0U;
    std::size_t spine_count = 0U;
    char epub2_cover_id[97] = {};
    bool in_manifest = false, in_spine = false, in_metadata = false, in_guide = false;
    char guide_cover[EPUB_ARCHIVE_PATH_CAPACITY] = {};
    std::size_t cursor = 0U;
    std::string_view tag;
    while (next_tag(xml, length, cursor, tag)) {
        if (tag.size() >= 3U && (tag.substr(0U, 3U) == "!--" || tag[0] == '?' || tag[0] == '!')) { continue; }
        bool closing = false;
        const auto name = tag_name(tag, closing);
        if (equal_folded(name, "manifest")) { in_manifest = !closing; continue; }
        if (equal_folded(name, "spine")) { in_spine = !closing; continue; }
        if (equal_folded(name, "metadata")) { in_metadata = !closing; continue; }
        if (equal_folded(name, "guide")) { in_guide = !closing; continue; }
        if (in_metadata && !closing && equal_folded(name, "meta")) {
            std::string_view meta_name, content;
            if (attribute(tag, "name", meta_name) && equal_folded(meta_name, "cover") &&
                attribute(tag, "content", content)) {
                copy_xml_value(content, epub2_cover_id, sizeof(epub2_cover_id));
            }
        } else if (in_manifest && !closing && equal_folded(name, "item")) {
            if (manifest_count >= EPUB_MANIFEST_ITEM_LIMIT) { return false; }
            std::string_view id, href, media, properties;
            if (!attribute(tag, "id", id) || !attribute(tag, "href", href) ||
                !attribute(tag, "media-type", media)) { return false; }
            manifest_item item = {};
            char decoded[EPUB_ARCHIVE_PATH_CAPACITY] = {};
            if (!copy_xml_value(id, item.id, sizeof(item.id)) ||
                !copy_xml_value(href, decoded, sizeof(decoded)) ||
                !copy_xml_value(media, item.media, sizeof(item.media)) ||
                !epub_resolve_path(package_path, decoded, item.path, sizeof(item.path))) { return false; }
            item.document = equal_folded(item.media, "application/xhtml+xml") ||
                            equal_folded(item.media, "text/html");
            item.cover = attribute(tag, "properties", properties) && token_contains(properties, "cover-image");
            manifest[manifest_count++] = item;
        } else if (in_spine && !closing && equal_folded(name, "itemref")) {
            if (spine_count >= EPUB_SPINE_ITEM_LIMIT) { return false; }
            std::string_view idref, linear;
            if (!attribute(tag, "idref", idref)) { return false; }
            spine_ref value = {};
            if (!copy_xml_value(idref, value.id, sizeof(value.id))) { return false; }
            value.linear = !attribute(tag, "linear", linear) || !equal_folded(linear, "no");
            spine[spine_count++] = value;
        } else if (in_guide && !closing && equal_folded(name, "reference")) {
            std::string_view type, href;
            char decoded[EPUB_ARCHIVE_PATH_CAPACITY] = {};
            if (attribute(tag, "type", type) && equal_folded(type, "cover") &&
                attribute(tag, "href", href) && copy_xml_value(href, decoded, sizeof(decoded))) {
                epub_resolve_path(package_path, decoded, guide_cover, sizeof(guide_cover));
            }
        }
    }
    if (manifest_count == 0U || spine_count == 0U) { return false; }
    output.spine = epub_allocate_array<epub_spine_item>(spine_count);
    if (!output.spine) { return false; }
    for (std::size_t ref_index = 0U; ref_index < spine_count; ++ref_index) {
        const auto& ref = spine[ref_index];
        if (!ref.linear) { continue; }
        const auto found = std::find_if(manifest.get(), manifest.get() + manifest_count,
            [&ref](const manifest_item& item) { return std::strcmp(item.id, ref.id) == 0; });
        if (found == manifest.get() + manifest_count || !found->document) { return false; }
        epub_spine_item item = {};
        std::strcpy(item.path, found->path);
        output.spine[output.spine_count++] = item;
    }
    if (output.spine_count == 0U) { return false; }
    for (std::size_t item_index = 0U; item_index < manifest_count; ++item_index) {
        const auto& item = manifest[item_index];
        if (!item.cover && (epub2_cover_id[0] == '\0' || std::strcmp(item.id, epub2_cover_id) != 0)) { continue; }
        const auto encoding = cover_type(item.media, item.path);
        if (encoding != book_cover_encoding::none) {
            std::strcpy(output.cover_path, item.path);
            output.cover_encoding = encoding;
            break;
        } else if (item.document && output.cover_document_path[0] == '\0') {
            std::strcpy(output.cover_document_path, item.path);
        }
    }
    if (output.cover_path[0] == '\0' && output.cover_document_path[0] == '\0' && guide_cover[0] != '\0') {
        std::strcpy(output.cover_document_path, guide_cover);
    }
    return true;
}

book_cover_encoding epub_cover_encoding_from_path(const char* path)
{
    return path == nullptr ? book_cover_encoding::none : cover_type({}, path);
}

bool epub_parse_cover_document(const char* xml, std::size_t length,
                               const char* document_path, char* image_path,
                               std::size_t capacity)
{
    if (xml == nullptr || document_path == nullptr || image_path == nullptr ||
        length == 0U || length > EPUB_CONTAINER_XML_LIMIT) { return false; }
    std::size_t cursor = 0U;
    std::string_view tag;
    while (next_tag(xml, length, cursor, tag)) {
        bool closing = false;
        const auto name = tag_name(tag, closing);
        if (closing || (!equal_folded(name, "img") && !equal_folded(name, "image"))) { continue; }
        std::string_view source;
        if (!attribute(tag, equal_folded(name, "img") ? "src" : "href", source)) { continue; }
        char decoded[EPUB_ARCHIVE_PATH_CAPACITY] = {};
        if (!copy_xml_value(source, decoded, sizeof(decoded))) { return false; }
        return epub_resolve_path(document_path, decoded, image_path, capacity);
    }
    return false;
}

bool epub_xhtml_filter::emit(const char* data, std::size_t length)
{
    if (length == 0U) { return true; }
    if (output_size_ > EPUB_CONTENT_SIZE_LIMIT || length > EPUB_CONTENT_SIZE_LIMIT - output_size_ ||
        !sink_.write(sink_.context, data, length)) { return false; }
    output_size_ += length;
    last_output_ = data[length - 1U];
    wrote_text_ = true;
    return true;
}

bool epub_xhtml_filter::emit_codepoint(std::uint32_t codepoint)
{
    if (codepoint == 0U || codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) { return false; }
    if (codepoint == '\r' || codepoint == '\n' || codepoint == '\t' || codepoint == ' ' || codepoint == 0xa0U) {
        pending_space_ = wrote_text_ && last_output_ != '\n';
        return true;
    }
    if (codepoint < 0x20U || codepoint == 0x7fU) { return true; }
    if (pending_space_) {
        pending_space_ = false;
        if (!emit(" ", 1U)) { return false; }
    }
    char encoded[5] = {};
    return emit(encoded, utf8_encode(codepoint, encoded));
}

bool epub_xhtml_filter::emit_newline(bool paragraph)
{
    pending_space_ = false;
    if (!wrote_text_) { return true; }
    if (last_output_ != '\n' && !emit("\n", 1U)) { return false; }
    if (paragraph && last_output_ == '\n' && !emit("\n", 1U)) { return false; }
    return true;
}

bool epub_xhtml_filter::text_byte(std::uint8_t byte)
{
    if (utf8_length_ >= sizeof(utf8_)) { return false; }
    utf8_[utf8_length_++] = static_cast<char>(byte);
    std::uint32_t codepoint = 0U; std::size_t length = 0U;
    const auto decoded = utf8_decode(utf8_, utf8_length_, codepoint, length);
    if (decoded == utf8_decode_result::invalid) { return false; }
    if (decoded == utf8_decode_result::incomplete) { return true; }
    utf8_length_ = 0U;
    return ignored_depth_ != 0U || emit_codepoint(codepoint);
}

bool epub_xhtml_filter::finish_tag()
{
    if (token_overflow_) { return false; }
    std::string_view tag(token_, token_length_);
    bool closing = false;
    const auto name = tag_name(tag, closing);
    if (equal_folded(name, "script") || equal_folded(name, "style")) {
        if (closing) { if (ignored_depth_ != 0U) { --ignored_depth_; } }
        else if (ignored_depth_ != UINT16_MAX) { ++ignored_depth_; }
        return true;
    }
    if (ignored_depth_ != 0U) { return true; }
    if (equal_folded(name, "br")) { return emit_newline(false); }
    const bool block = equal_folded(name, "p") || equal_folded(name, "div") ||
        equal_folded(name, "li") || equal_folded(name, "blockquote") ||
        equal_folded(name, "section") || equal_folded(name, "article") ||
        (name.size() == 2U && (name[0] == 'h' || name[0] == 'H') && name[1] >= '1' && name[1] <= '6');
    return !block || !closing || emit_newline(false);
}

bool epub_xhtml_filter::finish_entity()
{
    if (token_overflow_ || token_length_ == 0U) { return false; }
    const std::string_view value(token_, token_length_);
    std::uint32_t codepoint = 0U;
    if (value == "amp") { codepoint = '&'; }
    else if (value == "lt") { codepoint = '<'; }
    else if (value == "gt") { codepoint = '>'; }
    else if (value == "quot") { codepoint = '"'; }
    else if (value == "apos") { codepoint = '\''; }
    else if (value == "nbsp") { codepoint = 0xa0U; }
    else if (value == "ndash") { codepoint = 0x2013U; }
    else if (value == "mdash") { codepoint = 0x2014U; }
    else if (value == "hellip") { codepoint = 0x2026U; }
    else if (value[0] == '#') {
        const bool hex = value.size() > 1U && (value[1] == 'x' || value[1] == 'X');
        const std::size_t start = hex ? 2U : 1U;
        if (start == value.size()) { return false; }
        for (std::size_t i = start; i < value.size(); ++i) {
            unsigned digit = 0U;
            if (value[i] >= '0' && value[i] <= '9') { digit = value[i] - '0'; }
            else if (hex && value[i] >= 'a' && value[i] <= 'f') { digit = value[i] - 'a' + 10U; }
            else if (hex && value[i] >= 'A' && value[i] <= 'F') { digit = value[i] - 'A' + 10U; }
            else { return false; }
            if (codepoint > (0x10ffffU - digit) / (hex ? 16U : 10U)) { return false; }
            codepoint = codepoint * (hex ? 16U : 10U) + digit;
        }
    } else {
        if (ignored_depth_ != 0U) { return true; }
        if (!emit("&", 1U) || !emit(token_, token_length_) || !emit(";", 1U)) { return false; }
        return true;
    }
    return ignored_depth_ != 0U || emit_codepoint(codepoint);
}

bool epub_xhtml_filter::feed(const std::uint8_t* data, std::size_t length, bool end)
{
    if ((data == nullptr && length != 0U) || (mode_ != mode::text && utf8_length_ != 0U)) { return false; }
    for (std::size_t i = 0U; i < length; ++i) {
        const char value = static_cast<char>(data[i]);
        if (mode_ == mode::comment) {
            if (comment_tail_ == 0U) { comment_tail_ = value == '-' ? 1U : 0U; }
            else if (comment_tail_ == 1U) { comment_tail_ = value == '-' ? 2U : 0U; }
            else if (value == '>') { mode_ = mode::text; comment_tail_ = 0U; }
            else { comment_tail_ = value == '-' ? 2U : 0U; }
            continue;
        }
        if (mode_ == mode::text) {
            if (utf8_length_ == 0U && value == '<') {
                mode_ = mode::tag; token_length_ = 0U; token_overflow_ = false;
                tag_quote_ = '\0'; continue;
            }
            if (utf8_length_ == 0U && value == '&') {
                mode_ = mode::entity; token_length_ = 0U; token_overflow_ = false; continue;
            }
            if (!text_byte(data[i])) { return false; }
        } else if (mode_ == mode::entity) {
            if (value == ';') {
                if (!finish_entity()) { return false; }
                mode_ = mode::text;
            } else if (value == '<' || value == '&' || std::isspace(static_cast<unsigned char>(value))) {
                return false;
            } else if (token_length_ < sizeof(token_)) { token_[token_length_++] = value; }
            else { token_overflow_ = true; }
        } else {
            if (tag_quote_ != '\0') {
                if (value == tag_quote_) { tag_quote_ = '\0'; }
                if (token_length_ < sizeof(token_)) { token_[token_length_++] = value; }
                else { token_overflow_ = true; }
            } else if (value == '\'' || value == '"') {
                tag_quote_ = value;
                if (token_length_ < sizeof(token_)) { token_[token_length_++] = value; }
                else { token_overflow_ = true; }
            } else if (value == '>' && (token_length_ < 3U || std::memcmp(token_, "!--", 3U) != 0)) {
                if (!finish_tag()) { return false; }
                mode_ = mode::text;
            } else if (token_length_ < sizeof(token_)) {
                token_[token_length_++] = value;
                if (token_length_ == 3U && std::memcmp(token_, "!--", 3U) == 0) {
                    mode_ = mode::comment; comment_tail_ = 0U;
                }
            } else { token_overflow_ = true; }
        }
    }
    return !end || (mode_ == mode::text && utf8_length_ == 0U);
}

bool epub_xhtml_filter::finish_chapter()
{
    if (mode_ != mode::text || utf8_length_ != 0U) { return false; }
    // Keep every spine start distinct, including markup-only chapters.
    if (!wrote_text_) { return emit("\n", 1U); }
    return emit_newline(true);
}

bool epub_cache_metadata_decode(const char* json, std::size_t length,
                                epub_cache_metadata& output)
{
    output = {};
    if (!bounded_json(json, length)) { return false; }
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (root == nullptr) { return false; }
    while (end < json + length && std::isspace(static_cast<unsigned char>(*end))) { ++end; }
    const auto* source = cJSON_GetObjectItemCaseSensitive(root, "source");
    const auto* fingerprint = cJSON_GetObjectItemCaseSensitive(source, "fingerprint");
    const auto* cache = cJSON_GetObjectItemCaseSensitive(root, "cache");
    const auto* progress = cJSON_GetObjectItemCaseSensitive(root, "progress");
    const auto* path = cJSON_GetObjectItemCaseSensitive(root, "canonical_path");
    const auto* algorithm = cJSON_GetObjectItemCaseSensitive(fingerprint, "algorithm");
    const auto* encoding = cJSON_GetObjectItemCaseSensitive(cache, "cover_encoding");
    const auto* complete = cJSON_GetObjectItemCaseSensitive(cache, "complete");
    std::uint64_t schema = 0U, parser = 0U, pagination = 0U, mtime = 0U;
    std::uint64_t head = 0U, middle = 0U, tail = 0U, spine = 0U, progress_spine = 0U;
    epub_cache_metadata value = {};
    bool valid = end == json + length && unique_keys(root) && unique_keys(source) &&
        unique_keys(fingerprint) && unique_keys(cache) && unique_keys(progress) &&
        number(root, "schema_version", UINT32_MAX, schema) && schema == EPUB_CACHE_SCHEMA_VERSION &&
        cJSON_IsString(path) && path->valuestring != nullptr &&
        std::strlen(path->valuestring) < sizeof(value.canonical_path) &&
        number(root, "parser_version", UINT32_MAX, parser) &&
        number(root, "pagination_version", UINT32_MAX, pagination) &&
        number(source, "size", BOOK_JSON_INTEGER_MAX, value.source.file_size) &&
        number(source, "mtime", BOOK_JSON_INTEGER_MAX, mtime) &&
        cJSON_IsString(algorithm) && algorithm->valuestring != nullptr &&
        std::strcmp(algorithm->valuestring, BOOK_FINGERPRINT_ALGORITHM) == 0 &&
        number(fingerprint, "head", UINT32_MAX, head) &&
        number(fingerprint, "middle", UINT32_MAX, middle) &&
        number(fingerprint, "tail", UINT32_MAX, tail) &&
        number(cache, "content_size", EPUB_CONTENT_SIZE_LIMIT, value.content_size) &&
        number(cache, "cover_size", EPUB_COVER_SIZE_LIMIT, value.cover_size) &&
        number(cache, "spine_count", EPUB_SPINE_ITEM_LIMIT, spine) &&
        cJSON_IsString(encoding) && encoding->valuestring != nullptr && cJSON_IsBool(complete) &&
        number(progress, "spine_index", EPUB_SPINE_ITEM_LIMIT - 1U, progress_spine) &&
        number(progress, "content_offset", EPUB_CONTENT_SIZE_LIMIT, value.progress.content_offset) &&
        number(progress, "linear_offset", EPUB_CONTENT_SIZE_LIMIT, value.progress.linear_offset);
    if (valid) {
        std::strcpy(value.canonical_path, path->valuestring);
        value.source.modified_time = static_cast<std::int64_t>(mtime);
        value.source.fingerprint = {static_cast<std::uint32_t>(head), static_cast<std::uint32_t>(middle), static_cast<std::uint32_t>(tail)};
        value.parser_version = static_cast<std::uint32_t>(parser);
        value.pagination_version = static_cast<std::uint32_t>(pagination);
        value.spine_count = static_cast<std::uint16_t>(spine);
        value.progress.spine_index = static_cast<std::uint16_t>(progress_spine);
        value.complete = cJSON_IsTrue(complete);
        if (std::strcmp(encoding->valuestring, "none") == 0) { value.cover_encoding = book_cover_encoding::none; }
        else if (std::strcmp(encoding->valuestring, "jpeg") == 0) { value.cover_encoding = book_cover_encoding::jpeg; }
        else if (std::strcmp(encoding->valuestring, "png") == 0) { value.cover_encoding = book_cover_encoding::png; }
        else { valid = false; }
        char canonical[BOOK_PATH_CAPACITY] = {};
        valid = valid && book_canonical_path(value.canonical_path, canonical, sizeof(canonical)) &&
            std::strcmp(canonical, value.canonical_path) == 0 && value.complete &&
            value.parser_version != 0U && value.pagination_version != 0U &&
            value.spine_count != 0U && value.content_size != 0U &&
            value.progress.spine_index < value.spine_count &&
            value.progress.linear_offset < value.content_size &&
            ((value.cover_encoding == book_cover_encoding::none && value.cover_size == 0U) ||
             (value.cover_encoding != book_cover_encoding::none && value.cover_size != 0U));
    }
    cJSON_Delete(root);
    if (valid) { output = value; }
    return valid;
}

bool epub_cache_metadata_encode(const epub_cache_metadata& value,
                                char* json, std::size_t capacity)
{
    if (json == nullptr || capacity == 0U || capacity > EPUB_CACHE_METADATA_CAPACITY ||
        !value.complete || value.spine_count == 0U || value.spine_count > EPUB_SPINE_ITEM_LIMIT ||
        value.content_size == 0U || value.content_size > EPUB_CONTENT_SIZE_LIMIT ||
        value.progress.spine_index >= value.spine_count || value.progress.linear_offset >= value.content_size) { return false; }
    cJSON* root = cJSON_CreateObject();
    auto* source = root == nullptr ? nullptr : cJSON_AddObjectToObject(root, "source");
    auto* fingerprint = source == nullptr ? nullptr : cJSON_AddObjectToObject(source, "fingerprint");
    auto* cache = root == nullptr ? nullptr : cJSON_AddObjectToObject(root, "cache");
    auto* progress = root == nullptr ? nullptr : cJSON_AddObjectToObject(root, "progress");
    const char* encoding = value.cover_encoding == book_cover_encoding::jpeg ? "jpeg" :
                           value.cover_encoding == book_cover_encoding::png ? "png" : "none";
    const bool valid = root != nullptr && source != nullptr && fingerprint != nullptr && cache != nullptr && progress != nullptr &&
        cJSON_AddNumberToObject(root, "schema_version", EPUB_CACHE_SCHEMA_VERSION) &&
        cJSON_AddStringToObject(root, "canonical_path", value.canonical_path) &&
        cJSON_AddNumberToObject(root, "parser_version", value.parser_version) &&
        cJSON_AddNumberToObject(root, "pagination_version", value.pagination_version) &&
        cJSON_AddNumberToObject(source, "size", static_cast<double>(value.source.file_size)) &&
        cJSON_AddNumberToObject(source, "mtime", static_cast<double>(value.source.modified_time)) &&
        cJSON_AddStringToObject(fingerprint, "algorithm", BOOK_FINGERPRINT_ALGORITHM) &&
        cJSON_AddNumberToObject(fingerprint, "head", value.source.fingerprint.head) &&
        cJSON_AddNumberToObject(fingerprint, "middle", value.source.fingerprint.middle) &&
        cJSON_AddNumberToObject(fingerprint, "tail", value.source.fingerprint.tail) &&
        cJSON_AddNumberToObject(cache, "content_size", static_cast<double>(value.content_size)) &&
        cJSON_AddNumberToObject(cache, "cover_size", static_cast<double>(value.cover_size)) &&
        cJSON_AddNumberToObject(cache, "spine_count", value.spine_count) &&
        cJSON_AddStringToObject(cache, "cover_encoding", encoding) &&
        cJSON_AddBoolToObject(cache, "complete", true) &&
        cJSON_AddNumberToObject(progress, "spine_index", value.progress.spine_index) &&
        cJSON_AddNumberToObject(progress, "content_offset", static_cast<double>(value.progress.content_offset)) &&
        cJSON_AddNumberToObject(progress, "linear_offset", static_cast<double>(value.progress.linear_offset)) &&
        cJSON_PrintPreallocated(root, json, static_cast<int>(capacity), true);
    cJSON_Delete(root);
    return valid;
}

void epub_spine_map_encode(const std::uint64_t* starts, std::uint16_t count,
                           std::uint64_t content_size, std::uint8_t* bytes,
                           std::size_t capacity, std::size_t& length)
{
    length = 0U;
    const std::size_t required = EPUB_SPINE_MAP_HEADER_SIZE + std::size_t(count) * sizeof(std::uint64_t);
    if (starts == nullptr || bytes == nullptr || count == 0U || count > EPUB_SPINE_ITEM_LIMIT ||
        content_size == 0U || capacity < required) { return; }
    std::memset(bytes, 0, required);
    std::memcpy(bytes, spine_magic, sizeof(spine_magic));
    encode_u32(bytes + 8U, 1U);
    encode_u32(bytes + 12U, count);
    book_encode_u64(bytes + 16U, content_size);
    for (std::uint16_t i = 0U; i < count; ++i) { book_encode_u64(bytes + EPUB_SPINE_MAP_HEADER_SIZE + i * 8U, starts[i]); }
    encode_u32(bytes + 24U, book_crc32(bytes + EPUB_SPINE_MAP_HEADER_SIZE, count * 8U));
    encode_u32(bytes + 28U, book_crc32(bytes, 28U));
    length = required;
}

bool epub_spine_map_decode(const std::uint8_t* bytes, std::size_t length,
                           std::uint64_t expected_content_size,
                           std::uint64_t* starts, std::size_t capacity,
                           std::uint16_t& count)
{
    count = 0U;
    if (bytes == nullptr || starts == nullptr || length < EPUB_SPINE_MAP_HEADER_SIZE ||
        std::memcmp(bytes, spine_magic, sizeof(spine_magic)) != 0 || decode_u32(bytes + 8U) != 1U ||
        decode_u32(bytes + 28U) != book_crc32(bytes, 28U) || book_decode_u64(bytes + 32U) != 0U ||
        book_decode_u64(bytes + 16U) != expected_content_size) { return false; }
    const auto decoded_count = decode_u32(bytes + 12U);
    if (decoded_count == 0U || decoded_count > EPUB_SPINE_ITEM_LIMIT || decoded_count > capacity ||
        length != EPUB_SPINE_MAP_HEADER_SIZE + decoded_count * 8U ||
        decode_u32(bytes + 24U) != book_crc32(bytes + EPUB_SPINE_MAP_HEADER_SIZE, decoded_count * 8U)) { return false; }
    std::uint64_t previous = 0U;
    for (std::uint32_t i = 0U; i < decoded_count; ++i) {
        const auto value = book_decode_u64(bytes + EPUB_SPINE_MAP_HEADER_SIZE + i * 8U);
        if ((i == 0U && value != 0U) || (i != 0U && value <= previous) || value >= expected_content_size) { return false; }
        starts[i] = value; previous = value;
    }
    count = static_cast<std::uint16_t>(decoded_count);
    return true;
}

epub_position epub_position_from_linear(const std::uint64_t* starts,
                                        std::uint16_t count,
                                        std::uint64_t linear_offset)
{
    epub_position result = {0U, linear_offset, linear_offset};
    if (starts == nullptr || count == 0U) { return result; }
    const auto* upper = std::upper_bound(starts, starts + count, linear_offset);
    result.spine_index = upper == starts ? 0U : static_cast<std::uint16_t>(upper - starts - 1U);
    result.content_offset = linear_offset - starts[result.spine_index];
    return result;
}
