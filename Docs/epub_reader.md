# How the EPUB Reader Works

This document describes the EPUB reading path implemented in the source. Reader recognizes `.epub` without regard to case and handles it as an unencrypted, reflowable EPUB2 or EPUB3 publication.

## Opening and data flow

1. BooksApp obtains a normalized `.epub` path and format from BookCatalogService; FileApp recognizes `.txt` and `.epub` without regard to case and constructs the path from its directory entry. Both call `reader_make_launch_context()` with the complete logical path, `media_generation`, and format, then pass that generic context to `app_request_launch()`.
2. App Runtime retains the bounded context until the deferred switch and calls the target application's `prepare_launch()`. `ReaderApp` validates and copies its own typed payload. `ReaderApp::on_open()` then creates a new `session_id`, obtains the shared text layout, and submits an EPUB open command to `BookService`. ReaderApp does not read ZIP, XML, or FATFS directly.
3. The BookService `book_worker` opens the ZIP, finds the OPF path through `META-INF/container.xml`, parses the OPF manifest, spine, and cover item, and processes XHTML in spine order.
4. A streaming XHTML filter converts the documents into continuous UTF-8 text. Derived text, spine mapping, and the compressed cover are stored on the SD card. Complete metadata is replaced last.
5. After derived text is ready, the existing `book_index_engine` builds its pagination index. ReaderApp uses the shared `reader_paginator` for the current page, and the UI receives the same `reader_view_state` and `reader_page` used for TXT.
6. ReaderApp reads derived text or cover data through BookService in 2048-byte result blocks. Queues carry only `result_handle`; Reader consumes a resolved result synchronously in its event callback and verifies `session_id`, `request_id`, `media_generation`, and offset.

The body path is:

```text
BooksApp / FileApp
  → reader_make_launch_context
  → app_request_launch
  → generic app_launch_context
  → ReaderApp
  → BookService / book_worker
  → epub_zip_archive
  → container.xml / OPF / manifest / spine
  → epub_xhtml_filter
  → derived text on SD
  → book_index_engine + reader_paginator
  → reader_page
  → UI frame pool
  → PaperMono renderer
```

## Module boundaries

| Layer | Module | Responsibility |
| --- | --- | --- |
| Core | `book_file_format`, `book_types`, `text_paginator` | Defines book formats, small cross-layer events, EPUB position/value types, and shared TXT/EPUB pagination. |
| App | `BooksApp`, `FileApp` | Select a catalog or directory item and construct the same typed Reader launch payload. |
| App | App Runtime | Owns generic launch bytes for one deferred switch and manages switching, return history, and Mooncake lifecycle without Reader-specific fields or branches. |
| App | `ReaderApp` | Groups session, page, content-request, book/index, cover, navigation, and presentation state; manages loading, paging, errors, and asynchronous result lifetimes. |
| Service | `book_catalog_service` | Scans selected EPUB paths, validates or builds shelf cover caches serially, persists catalog state, and pauses before Reader starts. |
| Service | `book_service` | Serializes Reader parsing, caching, page indexing, and block reads and publishes small events and result handles. |
| Service | `epub_archive` | Validates ZIP32 directories and local headers, finds entries, and streams Stored or Deflate data while checking CRC. |
| Service | `epub_format` | Parses container and OPF files, normalizes internal paths, finds cover references, converts XHTML to UTF-8 text, and encodes or decodes EPUB metadata and the spine map. |
| Service | `epub_cache_engine` | Coordinates the parser state machine and SD-derived files, validates source and cache data, and stores EPUB positions. |
| UI | `reader_cover`, `reader_renderer` | Keeps generation-tagged, reference-counted compressed covers in PSRAM, validates image headers, and renders the cover, body, and top menu. |
| HAL | Storage / Display | Reads and writes ordinary files by offset. M5GFX decodes JPEG or PNG into an eight-bit grayscale temporary canvas and quantizes or dithers it into the packed 2bpp display framebuffer. The HAL does not parse EPUB. |

TXT body blocks still come directly from StorageService. EPUB ZIP, package, XHTML, and derived-cache code are separate modules. Both formats share ReaderApp interaction state, TextPaginator, page values, BookIndex mechanisms, UI frames, and the Renderer.

## ZIP, package, and path handling

ZIP access locates entries through the central directory and rechecks the local header, name, compression method, flags, sizes, and CRC during extraction. Stored and raw Deflate are supported. Deflate uses the Apache-2.0-licensed miniz `tinfl` in ESP-IDF `esp_rom`, with a 32 KiB dictionary and 2048-byte input and delivery blocks. Encrypted entries, multi-disk ZIP, ZIP64 sizes, and other methods are rejected.

`container.xml` supplies the OPF rootfile. The OPF parser accepts default or prefixed namespace names and reads the manifest and linear spine. An internal reference has its query and fragment removed and percent escapes decoded. It is then resolved against the containing file's directory while handling `./`, `../`, and repeated `/`. Paths that escape the archive root, contain backslashes or control characters, are absolute, or name external URIs are rejected.

## XHTML conversion

Each spine entry streams directly from decompression into the XHTML filter, so a complete chapter is not held in RAM. The filter:

- preserves text in elements such as `body`, `span`, `strong`, and `em`;
- inserts a newline after `p`, `div`, `h1` through `h6`, `li`, `blockquote`, `section`, and `article`;
- turns `br` into a newline and normalizes ordinary whitespace;
- ignores markup, XML declarations, comments, and `script` or `style` contents;
- decodes `amp`, `lt`, `gt`, `quot`, `apos`, `nbsp`, `ndash`, `mdash`, `hellip`, and decimal or hexadecimal numeric entities;
- carries UTF-8 sequences across blocks using the project's decoder and fails body parsing on an invalid sequence.

Spine text is appended to one `epub_content.txt`. Chapter starts are stored separately in `epub_spine.map`, so paging remains continuous and chapter boundaries do not change Reader previous/next behavior.

## SD-derived files and cache reuse

The Book ID is the SHA-256 of the normalized complete path, so equal names in different directories use different cache directories. EPUB files are stored as:

```text
/.system/books/<book_id>/
  epub_metadata.json
  epub_content.txt
  epub_spine.map
  epub_cover.bin        # when a usable cover exists
  metadata.json         # shared page-index metadata
  pages.idx             # shared page index
```

Initial parsing writes `.tmp` files. Derived body text, the spine map, and the cover replace their targets before `epub_metadata.json` is replaced last. Temporary output without complete metadata is never reused. The source EPUB is neither loaded wholly into RAM nor unpacked as a complete directory; the SD card keeps only derived data needed for reading.

Every open checks the normalized path, source size, mtime, three 4 KiB CRC32 fingerprints at the head, middle, and tail, parser and pagination versions, derived file sizes, and the spine-map header and CRC. A changed mtime with matching fingerprints updates metadata and reuses the cache. A fingerprint, size, schema, or parser-version mismatch reparses the source. EPUB cache schema is `2`; older derived body, cover, position, and index data are not reused. Reparse starts from logical page 0 when a usable cover exists, or body page 1 otherwise, and rebuilds the shared page index from the beginning.

## Reading progress and indexes

Progress in `epub_metadata.json` contains:

```text
spine_index
content_offset
linear_offset
at_cover
```

`linear_offset` is a page-start position in the derived continuous body. Saving maps it through `epub_spine.map` to `spine_index + content_offset`; loading recomputes and checks that all three agree. `at_cover` distinguishes logical page 0 from body page 1 because both may correspond to body offset 0. On a normal close, Reader submits the completed page start and cover state to BookService. A `pagination_version` mismatch resets progress and rebuilds the page index, with the position set to page 0 when a cover is available.

The shared `book_index_engine` creates `pages.idx` over `epub_content.txt`, so body page-number lookup, cross-chapter previous navigation, and total page count use the TXT mechanism. The cover is logical page 0, is excluded from the body total, and hides the status-bar page number. The right zone enters body page 1. Restoring a later body page does not preload the cover. After navigating backward to body page 1, one more previous action reads the cover from the SD card on demand and shows page 0. Closing records cover and body-page-1 positions separately, so reopening body page 1 does not mistake it for the cover.

## Cover handling

An EPUB3 cover comes from a manifest item with `properties="cover-image"`. EPUB2 also accepts `meta name="cover"` and image references from cover XHTML or a guide entry. The resolved image must be JPEG or PNG.

The compressed cover is streamed first to `epub_cover.bin` on the SD card and later returned by BookService in 2048-byte blocks. In the PaperMono configuration, the Reader cover store allocates only from PSRAM and retains at most two generation-tagged, reference-counted compressed slots. Each cover is limited to 512 KiB and is released when Reader closes. PNG requires a valid IHDR. JPEG must have an SOF marker within the first 64 KiB. Width and height are each limited to 4096 pixels.

M5GFX centers and scales the image to the content region in an eight-bit grayscale PSRAM canvas. Reader quantizes luminance into Black, Dark Gray, Light Gray, and White in the shared 2bpp framebuffer, then the SSD1677 backend uses the real `0xD7` four-level waveform. The image does not cover the shared status bar. BooksApp reuses the same image API with an explicit `mono_dither` mode, which projects shelf covers to Black/White through a fixed 4×4 Bayer threshold and keeps shelf updates on the monochrome path.

If cover-entry decompression, CRC, SD writing, header validation, or image decoding fails, the body remains readable. Parsing discards an unusable cover; a display-time failure enters the body or shows the cover fallback, and the right zone still opens the body.

## Tasks, memory, and lifecycle

Reader-side EPUB parsing adds no FreeRTOS task. The BookIndex worker is extended and named `book_worker`; it has a 12288-byte stack and priority `2`. It serially processes BookService commands, the EPUB state machine, and page indexing. BookCatalogService has a separate bounded worker for SD scanning and serial shelf-cache enrichment; BooksApp pauses it and waits for the idle acknowledgement before opening Reader, so two cache engines do not write the same book directory concurrently. Each worker loop yields between bounded steps. Mooncake callbacks do not scan ZIP, XML, or XHTML and do not decode images. Cover decoding runs in the UI renderer task.

Fixed or explicitly bounded capacities are:

| Item | Limit |
| --- | ---: |
| ZIP entries | 512 |
| EPUB internal path | 256 bytes plus null terminator |
| `container.xml` | 64 KiB |
| OPF | 256 KiB |
| Manifest items | 512, matched incrementally without retaining the full manifest table |
| Spine items | 512, with reference and path tables allocated for the actual count |
| One decompressed spine XHTML | 8 MiB |
| Derived continuous body | 64 MiB |
| XHTML/XML token | 256 bytes |
| ZIP / BookService data block | 2048 bytes |
| Deflate dictionary | 32 KiB |
| Compressed cover | 512 KiB |
| Cover grayscale canvas | Content-region size, currently 480 × 760 bytes |
| Cover width or height | 4096 pixels |
| BookService content result pool | 2 slots |

The ZIP entry table, XML buffers, spine-reference and path tables, and Deflate dictionary use explicit bounded allocations. PaperMono builds allocate these larger parser objects from PSRAM. The OPF is scanned three bounded times to count items, collect spine references, and match manifest items without retaining a full manifest table. Allocation failure returns an error instead of growing C++ containers. The complete derived body and page index remain on the SD card.

Reader checks SD state through `media_generation`. After removal or replacement, later worker I/O fails; Reader rejects an old session, request, generation, or released handle. `reader_runtime_state` is value-reset on open and close, while `session_serial_` continues across resets so a late result cannot join a later session.

## Supported scope

The implemented model accepts unencrypted reflowable EPUB2/EPUB3 publications using ZIP32, Stored or Deflate entries, a linear spine, UTF-8 XHTML text, and JPEG or PNG covers.

The reading model does not include a table-of-contents UI, chapter selection, search, named bookmarks, annotations, font or spacing controls, CSS layout, body images, tables, body SVG, audio, video, JavaScript, DRM, encrypted entries, fixed layout, media overlays, external links, network resources, or OPDS. Nonlinear spine items are skipped. Code points outside the active font continue to use the existing glyph fallback.

Compatibility boundaries include ZIP64, multi-disk ZIP, methods other than Stored or Deflate, packages, entries, or paths over the listed capacities, JPEG SOF beyond the first 64 KiB, and documents that require CSS or browser layout to preserve their body structure. Source fingerprinting samples only 4 KiB at the head, middle, and tail. A content change outside all samples is not detected when size and mtime also remain unchanged. Parser errors appear as invalid EPUB or unsupported format; SD I/O and media-generation failures appear as storage errors.

Source entry points: [file recognition](../core/book_file_format.cpp), [generic App Runtime](../app/app.cpp), [Reader launch data](../app/reader/reader_launch.hpp), [ReaderApp](../app/reader/reader_app.cpp), [BookService](../services/book/book_service.cpp), [ZIP](../services/book/epub_archive.cpp), [package/XHTML/metadata](../services/book/epub_format.cpp), [SD cache](../services/book/epub_cache_engine.cpp), [cover store](../ui/paper_mono/reader_cover.cpp), and [Reader renderer](../ui/paper_mono/views/reader_renderer.cpp).
