#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <cJSON.h>

#include "epub_archive.hpp"
#include "epub_cache_engine.hpp"
#include "epub_format.hpp"

namespace {
unsigned epub_checks = 0U;
#define EPUB_VERIFY(c) do { ++epub_checks; if (!(c)) { \
    std::printf("TEST_FAILURE epub line=%d: %s\n", __LINE__, #c); std::fflush(stdout); std::abort(); } } while (0)

void put16(std::string& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value)); out.push_back(static_cast<char>(value >> 8U));
}
void put32(std::string& out, std::uint32_t value)
{
    for (unsigned i = 0U; i < 4U; ++i) { out.push_back(static_cast<char>(value >> (i * 8U))); }
}

std::string raw_deflate_stored(const std::string& plain)
{
    EPUB_VERIFY(plain.size() <= UINT16_MAX);
    std::string out(1U, '\x01');
    const auto length = static_cast<std::uint16_t>(plain.size());
    put16(out, length); put16(out, static_cast<std::uint16_t>(~length)); out += plain;
    return out;
}

struct zip_item {
    std::string name, plain, packed;
    std::uint32_t offset = 0U;
    std::uint16_t method = 0U;
};

class zip_builder {
public:
    void add(std::string name, std::string plain, bool deflate = false)
    {
        zip_item item = {std::move(name), std::move(plain), {}, 0U,
                         static_cast<std::uint16_t>(deflate ? 8U : 0U)};
        item.packed = deflate ? raw_deflate_stored(item.plain) : item.plain;
        items_.push_back(std::move(item));
    }
    std::string finish()
    {
        std::string out;
        for (auto& item : items_) {
            item.offset = out.size();
            put32(out, 0x04034b50U); put16(out, 20U); put16(out, 0U); put16(out, item.method);
            put16(out, 0U); put16(out, 0U); put32(out, book_crc32(item.plain.data(), item.plain.size()));
            put32(out, item.packed.size()); put32(out, item.plain.size());
            put16(out, item.name.size()); put16(out, 0U); out += item.name; out += item.packed;
        }
        const auto central_offset = static_cast<std::uint32_t>(out.size());
        for (const auto& item : items_) {
            put32(out, 0x02014b50U); put16(out, 20U); put16(out, 20U); put16(out, 0U);
            put16(out, item.method); put16(out, 0U); put16(out, 0U);
            put32(out, book_crc32(item.plain.data(), item.plain.size()));
            put32(out, item.packed.size()); put32(out, item.plain.size());
            put16(out, item.name.size()); put16(out, 0U); put16(out, 0U); put16(out, 0U);
            put16(out, 0U); put32(out, 0U); put32(out, item.offset); out += item.name;
        }
        const auto central_size = static_cast<std::uint32_t>(out.size()) - central_offset;
        put32(out, 0x06054b50U); put16(out, 0U); put16(out, 0U);
        put16(out, items_.size()); put16(out, items_.size()); put32(out, central_size);
        put32(out, central_offset); put16(out, 0U);
        return out;
    }
private:
    std::vector<zip_item> items_;
};

struct memory_files {
    std::map<std::string, std::string, std::less<>> files;
    std::uint32_t generation = 1U;
    std::int64_t mtime = 77;
    unsigned reads = 0U, writes = 0U, replacements = 0U;
    bool removed = false;

    static bool valid(void* context, std::uint32_t generation)
    {
        const auto& self = *static_cast<memory_files*>(context);
        return !self.removed && generation == self.generation;
    }
    static esp_err_t read(void* context, std::uint32_t generation, const char* path,
                          std::uint64_t offset, void* data, std::size_t capacity,
                          std::size_t& length, std::uint64_t& size, std::int64_t& mtime)
    {
        auto& self = *static_cast<memory_files*>(context);
        if (!valid(context, generation)) { return ESP_ERR_INVALID_STATE; }
        ++self.reads;
        const auto found = self.files.find(path);
        if (found == self.files.end()) { return ESP_ERR_NOT_FOUND; }
        size = found->second.size(); mtime = self.mtime;
        if (offset > size) { return ESP_ERR_INVALID_SIZE; }
        length = std::min<std::size_t>(capacity, size - offset);
        if (length != 0U) { std::memcpy(data, found->second.data() + offset, length); }
        return ESP_OK;
    }
    static esp_err_t write(void* context, std::uint32_t generation, const char* path,
                           std::uint64_t offset, const void* data, std::size_t length, bool truncate)
    {
        auto& self = *static_cast<memory_files*>(context);
        if (!valid(context, generation) || length > 4096U) { return ESP_ERR_INVALID_STATE; }
        ++self.writes;
        auto& file = self.files[path];
        if (truncate) { file.clear(); }
        file.resize(std::max<std::size_t>(file.size(), offset + length));
        if (length != 0U) { std::memcpy(file.data() + offset, data, length); }
        return ESP_OK;
    }
    static esp_err_t mkdir(void* context, std::uint32_t generation, const char*)
    { return valid(context, generation) ? ESP_OK : ESP_ERR_INVALID_STATE; }
    static esp_err_t replace(void* context, std::uint32_t generation, const char* from, const char* to)
    {
        auto& self = *static_cast<memory_files*>(context);
        if (!valid(context, generation)) { return ESP_ERR_INVALID_STATE; }
        const auto found = self.files.find(from);
        if (found == self.files.end()) { return ESP_ERR_NOT_FOUND; }
        ++self.replacements; self.files[to] = std::move(found->second); self.files.erase(found); return ESP_OK;
    }
    static void emit(void*, const book_service_event&) {}
    book_engine_io io() { return {this, read, write, mkdir, replace, valid, emit}; }
    epub_archive_io archive_io() { return {this, read, valid}; }
};

bool append_text(void* context, const char* data, std::size_t length)
{
    static_cast<std::string*>(context)->append(data, length); return true;
}

std::string cover_fixture()
{
    constexpr std::uint8_t bytes[] = {
        0x89U, 'P', 'N', 'G', 0x0dU, 0x0aU, 0x1aU, 0x0aU,
        0x00U, 0x00U, 0x00U, 0x0dU, 'I', 'H', 'D', 'R',
        0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U,
    };
    return {reinterpret_cast<const char*>(bytes), sizeof(bytes)};
}

std::string make_epub(bool cover = true, bool invalid_utf8 = false, char filler = 'a')
{
    const std::string container =
        "<?xml version='1.0'?><container><rootfiles><rootfile full-path='OPS/package/content.opf'/></rootfiles></container>";
    std::string package =
        "<package version='3.0'><metadata></metadata><manifest>"
        "<item id='one' href='../text/ch1.xhtml' media-type='application/xhtml+xml'/>"
        "<item id='two' href='../text/../text/ch2.xhtml' media-type='application/xhtml+xml'/>";
    if (cover) { package += "<item id='cover' href='../images/cover.png' media-type='image/png' properties='cover-image'/>"; }
    package += "</manifest><spine><itemref idref='one'/><itemref idref='two'/></spine></package>";
    std::string chapter1 = "<html><body><h1>第一章</h1><p>" + std::string(2040U, filler) +
        "中文 &amp; text</p><script>BAD SCRIPT</script></body></html>";
    if (invalid_utf8) { chapter1.insert(chapter1.find("text"), 1U, static_cast<char>(0xff)); }
    const std::string chapter2 = "<html><body><div>World<br/>Next</div><style>BAD STYLE</style></body></html>";
    zip_builder zip;
    zip.add("mimetype", "application/epub+zip");
    zip.add("META-INF/container.xml", container);
    zip.add("OPS/package/content.opf", package, true);
    zip.add("OPS/text/ch1.xhtml", chapter1, true);
    zip.add("OPS/text/ch2.xhtml", chapter2);
    if (cover) { zip.add("OPS/images/cover.png", cover_fixture(), false); }
    return zip.finish();
}

void run_cache(epub_cache_engine& cache, esp_err_t expected = ESP_OK)
{
    esp_err_t error = ESP_OK;
    unsigned steps = 0U;
    while (cache.working() && steps++ < 10000U && error == ESP_OK) { error = cache.step(); }
    EPUB_VERIFY(error == expected);
    EPUB_VERIFY(expected != ESP_OK || cache.ready());
}

void test_paths_package_and_text()
{
    char path[EPUB_ARCHIVE_PATH_CAPACITY] = {};
    EPUB_VERIFY(epub_resolve_path("OPS/package/content.opf", "../text//./chapter%201.xhtml#part", path, sizeof(path)));
    EPUB_VERIFY(std::strcmp(path, "OPS/text/chapter 1.xhtml") == 0);
    EPUB_VERIFY(!epub_resolve_path("content.opf", "../escape.xhtml", path, sizeof(path)));
    EPUB_VERIFY(!epub_resolve_path("OPS/content.opf", "https://example.invalid/a", path, sizeof(path)));
    EPUB_VERIFY(epub_normalize_archive_path("OPS//text/./a.xhtml", path, sizeof(path)) &&
                std::strcmp(path, "OPS/text/a.xhtml") == 0);

    const char* epub2 =
        "<package version='2.0'><!-- <item id='fake' href='bad.xhtml' media-type='application/xhtml+xml'> -->"
        "<metadata><meta name='cover' content='cover-page'/></metadata>"
        "<manifest><item id='cover-page' href='cover.xhtml' media-type='application/xhtml+xml'/>"
        "<item id='chapter' href='text/ch.xhtml' media-type='application/xhtml+xml'/></manifest>"
        "<spine><itemref idref='chapter'/></spine></package>";
    epub_package parsed = {};
    EPUB_VERIFY(epub_parse_package(epub2, std::strlen(epub2), "OPS/content.opf", parsed));
    EPUB_VERIFY(parsed.spine_count == 1U && std::strcmp(parsed.spine[0].path, "OPS/text/ch.xhtml") == 0);
    EPUB_VERIFY(std::strcmp(parsed.cover_document_path, "OPS/cover.xhtml") == 0);
    const char* cover_doc = "<html><body><svg><image xlink:href='images/cover.jpg'/></svg></body></html>";
    EPUB_VERIFY(epub_parse_cover_document(cover_doc, std::strlen(cover_doc), parsed.cover_document_path,
                                          path, sizeof(path)));
    EPUB_VERIFY(std::strcmp(path, "OPS/images/cover.jpg") == 0 &&
                epub_cover_encoding_from_path(path) == book_cover_encoding::jpeg);

    std::string large_package = "<package><manifest>";
    for (unsigned index = 0U; index < 305U; ++index) {
        large_package += "<item id='i" + std::to_string(index) + "' href='text/p" +
                         std::to_string(index) + ".html' media-type='application/xhtml+xml'/>";
    }
    large_package += "</manifest><spine>";
    for (unsigned index = 0U; index < 137U; ++index) {
        large_package += "<itemref idref='i" + std::to_string(index) + "'/>";
    }
    large_package += "</spine></package>";
    parsed = {};
    EPUB_VERIFY(epub_parse_package(large_package.data(), large_package.size(), "content.opf", parsed));
    EPUB_VERIFY(parsed.spine_count == 137U &&
                std::strcmp(parsed.spine[136].path, "text/p136.html") == 0);

    std::string oversized_package = "<package><manifest>";
    for (unsigned index = 0U; index <= EPUB_MANIFEST_ITEM_LIMIT; ++index) {
        oversized_package += "<item id='x" + std::to_string(index) + "' href='x" +
                             std::to_string(index) + ".html' media-type='application/xhtml+xml'/>";
    }
    oversized_package += "</manifest><spine><itemref idref='x0'/></spine></package>";
    parsed = {};
    EPUB_VERIFY(!epub_parse_package(oversized_package.data(), oversized_package.size(),
                                    "content.opf", parsed));

    std::string text;
    epub_xhtml_filter filter({&text, append_text});
    const std::string html = "<?xml version='1.0'?><body data-note='1 > 0'><h1>标题</h1><p>Hello&nbsp;&amp; &#x4E2D;</p>"
                             "<div>A<br>B</div><script>hidden</script><style>hidden</style></body>";
    for (std::size_t i = 0U; i < html.size(); ++i) {
        EPUB_VERIFY(filter.feed(reinterpret_cast<const std::uint8_t*>(html.data() + i), 1U, false));
    }
    EPUB_VERIFY(filter.feed(nullptr, 0U, true) && filter.finish_chapter());
    EPUB_VERIFY(text.find("标题") != std::string::npos && text.find("Hello & 中") != std::string::npos);
    EPUB_VERIFY(text.find("A\nB") != std::string::npos && text.find("hidden") == std::string::npos);
    std::puts("PASS EPUB package/text: EPUB2 cover XHTML, 305 manifest/137 spine, bounded package, paths, entities, UTF-8");
}

void test_archive_and_cache()
{
    constexpr char source_path[] = "/Books/sample.epub";
    constexpr char id[] = "0000000000000000000000000000000000000000000000000000000000000000";
    constexpr char cache_root[] = "/.system/books/0000000000000000000000000000000000000000000000000000000000000000";
    memory_files sd;
    sd.files[source_path] = make_epub();
    {
        epub_zip_archive archive(sd.archive_io());
        EPUB_VERIFY(archive.open(source_path, 1U) == ESP_OK);
        const auto* deflated = archive.find("OPS/text/ch1.xhtml");
        EPUB_VERIFY(deflated != nullptr && deflated->method == 8U);
        std::vector<std::uint8_t> extracted(deflated->uncompressed_size);
        const auto extract_error = epub_zip_extract_memory(archive, *deflated, extracted.data(), extracted.size());
        if (extract_error != ESP_OK) { std::printf("EPUB_DEFLATE_ERROR=%s\n", esp_err_to_name(extract_error)); }
        EPUB_VERIFY(extract_error == ESP_OK);
        EPUB_VERIFY(std::string(reinterpret_cast<char*>(extracted.data()), extracted.size()).find("第一章") != std::string::npos);
    }

    auto cache = std::unique_ptr<epub_cache_engine>(new (std::nothrow) epub_cache_engine(sd.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK && cache->working());
    run_cache(*cache);
    const std::string root(cache_root);
    const auto& content = sd.files.at(root + "/epub_content.txt");
    EPUB_VERIFY(content.find("第一章") != std::string::npos && content.find("中文 & text") != std::string::npos);
    EPUB_VERIFY(content.find("World\nNext") != std::string::npos &&
                content.find("BAD SCRIPT") == std::string::npos && content.find("BAD STYLE") == std::string::npos);
    EPUB_VERIFY(sd.files.at(root + "/epub_cover.bin") == cover_fixture());
    epub_cache_metadata metadata = {};
    const auto& json = sd.files.at(root + "/epub_metadata.json");
    EPUB_VERIFY(epub_cache_metadata_decode(json.data(), json.size(), metadata));
    EPUB_VERIFY(metadata.spine_count == 2U && metadata.cover_encoding == book_cover_encoding::png &&
                metadata.progress_at_cover);
    std::uint64_t starts[EPUB_SPINE_ITEM_LIMIT] = {}; std::uint16_t count = 0U;
    const auto& map = sd.files.at(root + "/epub_spine.map");
    EPUB_VERIFY(epub_spine_map_decode(reinterpret_cast<const std::uint8_t*>(map.data()), map.size(),
                                      content.size(), starts, EPUB_SPINE_ITEM_LIMIT, count));
    EPUB_VERIFY(count == 2U && starts[0] == 0U && starts[1] > starts[0]);
    EPUB_VERIFY(cache->save(starts[1] + 2U, false) == ESP_OK);
    const auto& saved_json = sd.files.at(root + "/epub_metadata.json");
    EPUB_VERIFY(epub_cache_metadata_decode(saved_json.data(), saved_json.size(), metadata));
    EPUB_VERIFY(metadata.progress.spine_index == 1U && metadata.progress.content_offset == 2U &&
                metadata.progress.linear_offset == starts[1] + 2U && !metadata.progress_at_cover);
    const auto writes = sd.writes;
    cache.reset(new (std::nothrow) epub_cache_engine(sd.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK && cache->ready() && !cache->rebuilt());
    EPUB_VERIFY(sd.writes == writes && cache->metadata().progress.linear_offset == starts[1] + 2U &&
                !cache->metadata().progress_at_cover);
    EPUB_VERIFY(cache->save(1U, true) == ESP_ERR_INVALID_ARG);
    EPUB_VERIFY(cache->save(0U, true) == ESP_OK && cache->metadata().progress_at_cover);

    metadata = cache->metadata();
    metadata.pagination_version = BOOK_PAGINATION_VERSION - 1U;
    char stale_json[EPUB_CACHE_METADATA_CAPACITY] = {};
    EPUB_VERIFY(epub_cache_metadata_encode(metadata, stale_json, sizeof(stale_json)));
    sd.files[root + "/epub_metadata.json"] = stale_json;
    cache.reset(new (std::nothrow) epub_cache_engine(sd.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK &&
                cache->ready() && cache->rebuilt());
    EPUB_VERIFY(cache->metadata().progress.linear_offset == 0U && cache->metadata().progress_at_cover);

    // An older EPUB cache schema is not migrated or partially reused. It
    // starts a complete EPUB parse and produces a fresh page-0 position.
    const auto current_json = sd.files.at(root + "/epub_metadata.json");
    cJSON* old_root = cJSON_ParseWithLength(current_json.data(), current_json.size());
    EPUB_VERIFY(old_root != nullptr);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(old_root, "schema_version"),
                         EPUB_CACHE_SCHEMA_VERSION - 1U);
    char old_json[EPUB_CACHE_METADATA_CAPACITY] = {};
    EPUB_VERIFY(cJSON_PrintPreallocated(old_root, old_json, sizeof(old_json), true));
    cJSON_Delete(old_root);
    sd.files[root + "/epub_metadata.json"] = old_json;
    cache.reset(new (std::nothrow) epub_cache_engine(sd.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK && cache->working());
    run_cache(*cache);
    EPUB_VERIFY(cache->rebuilt() && cache->metadata().progress.linear_offset == 0U &&
                cache->metadata().progress_at_cover);

    // Same-size content changes are detected even when FAT mtime is unchanged.
    sd.files[source_path] = make_epub(true, false, 'b');
    cache.reset(new (std::nothrow) epub_cache_engine(sd.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK && cache->working());
    run_cache(*cache);
    EPUB_VERIFY(cache->rebuilt() && cache->metadata().progress.linear_offset == 0U &&
                cache->metadata().progress_at_cover);

    // A source stat change with identical three-point fingerprint keeps the parsed cache.
    ++sd.mtime;
    cache.reset(new (std::nothrow) epub_cache_engine(sd.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK && cache->ready() && !cache->rebuilt());
    EPUB_VERIFY(cache->metadata().source.modified_time == sd.mtime);

    // No cover remains a valid, readable EPUB.
    memory_files no_cover;
    no_cover.files[source_path] = make_epub(false);
    cache.reset(new (std::nothrow) epub_cache_engine(no_cover.io()));
    EPUB_VERIFY(cache != nullptr && cache->open(source_path, id, 1U) == ESP_OK); run_cache(*cache);
    EPUB_VERIFY(cache->metadata().cover_encoding == book_cover_encoding::none &&
                !cache->metadata().progress_at_cover && cache->save(0U, true) == ESP_ERR_INVALID_ARG);
    std::puts("PASS EPUB archive/cache: Stored+Deflate, SD artifacts, spine map, cover/no-cover, save/restore, fast reuse");
}

void test_failures()
{
    constexpr char path[] = "/bad.epub";
    constexpr char id[] = "1111111111111111111111111111111111111111111111111111111111111111";
    {
        memory_files corrupt;
        corrupt.files[path] = make_epub();
        auto& zip = corrupt.files[path];
        const auto location = zip.find("World");
        EPUB_VERIFY(location != std::string::npos);
        zip[location] ^= 1U;
        auto cache = std::unique_ptr<epub_cache_engine>(new (std::nothrow) epub_cache_engine(corrupt.io()));
        EPUB_VERIFY(cache != nullptr && cache->open(path, id, 1U) == ESP_OK);
        run_cache(*cache, ESP_ERR_INVALID_CRC);
    }
    {
        memory_files missing;
        zip_builder empty;
        empty.add("mimetype", "application/epub+zip");
        missing.files[path] = empty.finish();
        auto cache = std::unique_ptr<epub_cache_engine>(new (std::nothrow) epub_cache_engine(missing.io()));
        EPUB_VERIFY(cache != nullptr && cache->open(path, id, 1U) == ESP_ERR_NOT_FOUND);
    }
    {
        memory_files invalid;
        invalid.files[path] = make_epub(false, true);
        auto cache = std::unique_ptr<epub_cache_engine>(new (std::nothrow) epub_cache_engine(invalid.io()));
        EPUB_VERIFY(cache != nullptr && cache->open(path, id, 1U) == ESP_OK);
        run_cache(*cache, ESP_ERR_INVALID_ARG);
    }
    {
        memory_files bad_cover;
        bad_cover.files[path] = make_epub();
        auto& source = bad_cover.files[path];
        const auto cover = source.find(cover_fixture());
        EPUB_VERIFY(cover != std::string::npos);
        source[cover + cover_fixture().size() - 1U] ^= 1U;
        auto cache = std::unique_ptr<epub_cache_engine>(new (std::nothrow) epub_cache_engine(bad_cover.io()));
        EPUB_VERIFY(cache != nullptr && cache->open(path, id, 1U) == ESP_OK);
        run_cache(*cache);
        EPUB_VERIFY(cache->metadata().cover_encoding == book_cover_encoding::none);
    }
    std::puts("PASS EPUB failures: CRC corruption, missing container/OPF chain, invalid UTF-8, bad cover fallback");
}
}

std::string make_epub_fixture(bool cover, char filler)
{
    return make_epub(cover, false, filler);
}

void test_epub_support()
{
    test_paths_package_and_text();
    test_archive_and_cache();
    test_failures();
    std::printf("PASS EPUB checks=%u\n", epub_checks);
}
