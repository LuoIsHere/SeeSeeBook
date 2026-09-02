# SeeSeeBook

[中文文档](README_CN.md)

## Project Introduction

SeeSeeBook is firmware for the M5Stack PaperMono (ESP32-S3), built with ESP-IDF and Mooncake. It provides touch-operated utilities on a monochrome e-paper display: menu navigation, screen and front-light controls, RTC settings, battery information, and SD card directory browsing.

## Project Status

Version v0.1 is in early development. Unstable behavior, bugs, and compatibility issues may occur. The device implementation in this repository targets PaperMono. See [CHANGELOG.md](CHANGELOG.md) for a brief version history.

## Applications

| Registered App | Menu label | Purpose |
| --- | --- | --- |
| MenuApp | — | Opens at startup and provides application entries. |
| TestApp | Screen Setting | Tests display content and touch coordinates/duration, including long presses; offers OFF, 25%, 50%, 75%, and 100% front-light presets. |
| RTCSettingApp | RTC Setting | Reads and edits the local RTC date and time using a numeric keypad, with validation before saving. |
| BatteryApp | Battery | Displays battery percentage, voltage, a current field, and charging status; samples every 5 seconds while open. |
| FileApp | Files | Browses SD card directories and paginated file lists, with insertion/removal handling. |

Tap a menu entry to open an App; use `< Back` to return. A shared bottom status bar shows `HH:MM`, battery percentage, and a lightning symbol when charging is confirmed. On PaperMono, the current reading is unavailable and appears as `--`; an unavailable charging state appears as `Unknown`. BatteryApp monitors information only and does not configure charging.

- RTC Setting reads once when opened. Tap a date/time field, enter digits, and use the check mark to save; backspace clears the selected field, and Back cancels unsaved edits. Values use device-local time without timezone conversion.
- Files uses a FAT32 SD card, without automatic formatting. Directories appear before files, sorted by name. Tap a directory to enter it, use `..` to go up (no action at `/`), and use the bottom arrows to change pages. Long filenames and Chinese filenames are supported; names occupy one line and overflow is replaced with `...`. Tapping a file displays a three-second notification instead of opening its contents. Reinsert the card after a mount error.

## Architecture

The project is organized as separate ESP-IDF components, configured through `CMakeLists.txt` files.

```text
Apps (app/)
    |
    v
Services / System Runtime (services/, system/)
    |
    v
HAL (m5_hal/) --> PaperMono

Apps --> View State --> UI Renderer (ui/) --> Display HAL
```

- `app/` owns application logic and state, with Mooncake lifecycle integration.
- `services/` provides RTC, battery, storage, input, and front-light capabilities; `system/` coordinates runtime updates, event dispatch, and shared status.
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

## Secondary Development

### Add an App

1. Add logic under `app/<app_name>/`, deriving from [app_base](app/include/app/app_base.hpp), and define its View State in `ui/include/ui/`.
2. Add or reuse a Renderer in `ui/paper_mono/views/`, with layout in `ui/paper_mono/layout.hpp`; connect it to the existing UI presentation and interaction interfaces.
3. Add the App/view identifiers and a descriptor (`kind`, `view`, `name`) plus factory in [app/app_registry.cpp](app/app_registry.cpp). For a menu entry, add its target and label to [app/menu/menu_layout.hpp](app/menu/menu_layout.hpp); array order defines display order. The menu capacity is four entries, enforced at compile time. The capacity can be increased as needed.
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
