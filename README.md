# SeeSeeBook

[中文文档](README_CN.md)

> This is a university student's practice project, developed together with Codex. Some parts may be less mature; questions, feedback, and discussion are welcome.

## Project Introduction

SeeSeeBook is firmware for the M5Stack PaperMono (ESP32-S3), built with ESP-IDF and Mooncake. It provides an SD-backed Books shelf, file browsing, TXT/EPUB reading, device settings, and diagnostics on the PaperMono touch display.

## Project Status

The current version is V0.2 and targets M5Stack PaperMono. Book catalogs, parsed EPUB data, page indexes, and reading progress are stored under `/.system/books/` on the SD card. See [CHANGELOG.md](CHANGELOG.md) for the release summary and the Reader documents for storage formats and implementation boundaries.

## Applications

| Registered App | Entry | Purpose |
| --- | --- | --- |
| LauncherApp | Startup | Shows the fixed first-level `Books`, `File`, and `Menu` entries. |
| BooksApp | Books | Presents the SD book catalog as a 3 × 2 shelf, manages TXT/EPUB scan settings, previews TXT content, loads EPUB shelf covers, and opens Reader. |
| FileApp | File | Browses SD card directories and opens supported book files, with insertion/removal handling. |
| MenuApp | Menu | Shows `Screen Setting`, `RTC Setting`, `Battery`, and `Gray4 Test` in descriptor order. |
| TestApp | Screen Setting | Shows display and touch diagnostics and sets the front light to OFF, 25%, 50%, 75%, or 100%. |
| RTCSettingApp | RTC Setting | Reads and edits the local RTC date and time using a validated numeric keypad. |
| BatteryApp | Battery | Displays battery percentage, voltage, current availability, and charging state; samples every 5 seconds while open. |
| Gray4TestApp | Gray4 Test | Displays fixed four-level blocks and a stepped grayscale pattern for PaperMono hardware checks. |
| ReaderApp | Internal | Reads UTF-8 TXT and unencrypted reflowable EPUB files with paging, a logical EPUB cover page 0, and SD-backed progress. |

The shared bottom status bar shows `HH:MM`, battery percentage, and a lightning symbol when charging is confirmed. Its center can show a Books or Reader page context. PaperMono does not provide a current measurement through this implementation, so BatteryApp displays `--` for current; an unavailable charging state appears as `Unknown`.

- RTC Setting reads once when opened. Tap a date/time field, enter digits, and use the check mark to save; backspace clears the selected field, and Back cancels unsaved edits. Values use device-local time without timezone conversion.
- Books scans from the SD root according to the independently saved TXT and EPUB checkboxes. Hidden path components are excluded. The catalog and its settings are written atomically to `/.system/books/catalog_v1.bin`. Each page contains six items; TXT items show a bounded text preview and EPUB items show a cached cover when available. The shelf projects covers to stable 1-bit Bayer output for low-cost monochrome refreshes.
- File uses a FAT32 SD card without automatic formatting. Directories appear before files and are sorted by name. Tap a directory to enter it, use `..` to go up, and use the bottom arrows to change pages. Long and Chinese filenames are UTF-8 safely shortened. Tapping a `.txt` or `.epub` file opens Reader; other files display a temporary unsupported-file notification.
- Reader uses the left and right content areas for paging and the center area for its top menu. The menu return control is `<`. A usable EPUB cover is logical page 0 and hides the page number. Saved books resume at their validated page; incompatible pagination data is discarded and rebuilt from the beginning. See [TXT reader internals](Docs/txt_reader.md) and [EPUB reader internals](Docs/epub_reader.md).

## Supported book formats

| Format | Implemented scope | Boundaries |
| --- | --- | --- |
| TXT | UTF-8 with an optional BOM; LF, CRLF, and CR line endings; demand paging, page indexing, and resume position. | Other text encodings are not decoded. Invalid UTF-8 produces a Reader error. |
| EPUB | Unencrypted reflowable EPUB2/EPUB3 using ZIP32 Stored or Deflate entries, a linear spine, UTF-8 XHTML text, and JPEG/PNG covers. | DRM, encrypted entries, ZIP64, fixed-layout publications, CSS/browser layout, and body multimedia are not processed. |

Detailed parsing, storage, progress, and capacity limits are documented in [the TXT Reader description](Docs/txt_reader.md) and [the EPUB Reader description](Docs/epub_reader.md).

## PaperMono display

All drawing is stored in one 800 × 480 packed 2bpp framebuffer in PSRAM. Black-and-white UI frames use the monochrome OTP path; the renderer performs threshold-based monochrome cleanup as ghost debt accumulates. Frames containing light or dark gray use the SSD1677 `0xD7` four-level full refresh. Reader covers and Gray4 Test use real four-level output, while Books shelf covers deliberately use 1-bit dithering. The front light is disabled during full monochrome or Gray4 refreshes and restored afterward.

The PaperMono `0xFF` fast update transfers the complete 48,000-byte monochrome target into controller RAM1 before activation. UI dirty rectangles still limit logical drawing, request merging, and regional ghost accounting; they do not crop this controller transfer. Driver details and reference provenance are documented in [the PaperMono display backend](m5_hal/paper_mono/display/README.md).

## Architecture

The project is organized as separate ESP-IDF components, configured through `CMakeLists.txt` files.

```text
Launcher / Books / File / Menu / Reader (app/)
    |
    v
System Runtime + Services (system/, services/)
    |                |
    |                +--> SD catalog / EPUB cache / indexes / progress
    |
    v
HAL (m5_hal/) --> PaperMono

Apps --> View State --> UI Renderer (ui/) --> Display HAL
```

- `app/` owns application logic and state, with Mooncake lifecycle integration.
- `services/` provides RTC, battery, storage, input, front-light, book catalog, book indexing, and EPUB parsing/cache capabilities; `system/` coordinates initialization, runtime updates, event dispatch, and shared status.
- `ui/include/ui/` defines View States and UI interfaces; `ui/paper_mono/` handles device-specific drawing and layout.
- `m5_hal/include/hal/` defines hardware interfaces; `m5_hal/paper_mono/` implements PaperMono hardware access.
- `main/` initializes and runs the system; `core/` holds shared contracts and [project information](core/include/core/project_info.hpp).

App logic and Services are separated from device-specific HAL and Renderer implementations.

## Getting Started

### Prepare dependencies

Have Python 3 and Git available on `PATH`. From the project root, run the shared Python script [scripts/fetch_dependencies.py](scripts/fetch_dependencies.py).

Before running: the script can fetch and switch an existing clean dependency repository to the configured tag. It stops on local changes, an origin mismatch, or a target directory that is not a Git repository; it does not overwrite those directories.

Windows (Python 3 available as `python`):

```powershell
python scripts/fetch_dependencies.py
```

Unix-like systems (Python 3 available as `python3`):

```bash
python3 scripts/fetch_dependencies.py
```

The script reads [dependencies.json](dependencies.json), which currently lists:

| Dependency | Purpose | Source / tag | Destination |
| --- | --- | --- | --- |
| Mooncake | App lifecycle and switching framework | [Forairaaaaa/mooncake](https://github.com/Forairaaaaa/mooncake), `v2.3.3` | `dependencies/mooncake/` |

- Checks Git and the JSON configuration; installs no software.
- Creates `dependencies/` if missing, shallow-clones missing repositories at the configured tag, and verifies the origin and checked-out commit.
- Reuses matching clean repositories; otherwise fetches the requested tag and checks it out in detached mode. Errors stop the script.
- Initializes submodules only when enabled in the configuration; Mooncake has this disabled.

Add `--check` to either command to validate Git and configuration without downloading. M5Unified is declared separately in the component manifests and resolved by the ESP-IDF Component Manager, not this script. [dependencies.lock](dependencies.lock) records M5Unified 0.2.21 and its M5GFX 0.2.28 dependency.

### Build with ESP-IDF v5.5.5

After preparing dependencies, activate your ESP-IDF v5.5.5 environment, including its Python virtual environment. From the project root, run these commands in order:

```bash
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

- `set-target esp32s3` sets the target chip to ESP32-S3.
- `reconfigure` regenerates and checks project configuration and component dependencies.
- `build` compiles the project and generates firmware build artifacts.

### Run local integration tests

The test firmware exercises App navigation, Books catalog behavior, TXT/EPUB reading, SD lifecycle, progress and indexes, UI frames, Gray4 packing, image projection, and refresh policy in ESP32-S3 QEMU. With the ESP-IDF and QEMU tools available, run:

```bash
python ci/reader_tests/run_tests.py --build
```

A successful run exits with status `0` after printing `ALL_READER_TESTS_PASSED`. See [ci/README.md](ci/README.md) for tool discovery, reusable builds, generated files, and failure conditions. PaperMono waveforms, ghosting, front-light timing, and physical grayscale separation require device observation.

## Secondary Development

### Add an App

1. Add logic under `app/<app_name>/`, deriving from [app_base](app/include/app/app_base.hpp), and define its View State in `ui/include/ui/`.
2. Add or reuse a Renderer in `ui/paper_mono/views/`, with layout in `ui/paper_mono/layout.hpp`; connect it to the existing UI presentation and interaction interfaces.
3. Add the App/view identifiers and a descriptor (`kind`, `view`, `name`) plus factory in [app/app_registry.cpp](app/app_registry.cpp). First-level entries are defined in [app/launcher/launcher_layout.hpp](app/launcher/launcher_layout.hpp); secondary entries are defined in [app/menu/menu_layout.hpp](app/menu/menu_layout.hpp). Array order defines display order, and compile-time checks reject duplicate, unregistered, or oversized configurations.
4. Include the sources in the relevant component `CMakeLists.txt` files. Use existing event dispatch and non-blocking Mooncake lifecycle callbacks; access system capabilities through Services, not directly through HAL.

### Add a device

1. Implement the common hardware interfaces under `m5_hal/<device>/`, using `m5_hal/paper_mono/` as the existing example.
2. Implement or adapt Renderers and layout under `ui/<device>/`.
3. Reuse common App, Service, and event interfaces; keep device-specific dependencies out of those layers.
4. Update the necessary component/build configuration and document the supported device.

## References

- [ESP-IDF v5.5.5](https://github.com/espressif/esp-idf/tree/v5.5.5): the firmware development framework and build system used by SeeSeeBook.
- [M5Unified](https://github.com/m5stack/M5Unified): access to PaperMono peripherals through the device HAL.
- [Mooncake v2.3.3](https://github.com/Forairaaaaa/mooncake/tree/v2.3.3): application lifecycle and switching framework.
- [M5PaperMono-OTP-Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo): the reference for the PaperMono e-paper refresh backend; see the [driver documentation](m5_hal/paper_mono/display/README.md) for provenance.
- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader): an excellent open-source e-reader firmware project that inspired SeeSeeBook. Using it motivated the author to try developing reader firmware of their own.

These references do not imply official affiliation or endorsement.

## License

SeeSeeBook is licensed under the [MIT License](LICENSE). Third-party dependencies and adapted code retain their respective license and attribution requirements.
