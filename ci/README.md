# Local Reader Tests

## Purpose

`reader_tests` is a local ESP32-S3 QEMU integration test project. It compiles the production App Runtime, Launcher, Books catalog and shelf, Reader, storage, book, EPUB, and PaperMono UI sources with controlled storage and display adapters.

The test sources cover:

- `firmware/main/test_main.cpp`: Launcher/Books/Menu back-stack navigation, App switching and generic launch data, BookCatalogService discovery and persistence, Books/Reader integration, TXT and EPUB Reader flows, progress restore, cover page navigation, SD-card lifecycle, request timeout handling, and frame ownership.
- `firmware/main/books_ui_tests.cpp`: first-level and secondary entry descriptors, catalog scan filtering and sorting, Books three-column layout and pagination, settings transaction state, modal input capture, TXT preview, and two-line filename formatting.
- `firmware/main/book_tests.cpp`: book format detection, progress records, page-index records, index construction, and stale-record rejection.
- `firmware/main/epub_tests.cpp`: EPUB ZIP/container/OPF parsing, spine text extraction, cover extraction, cache generation, limits, and invalid or unsupported input handling.
- `firmware/main/reader_ui_tests.cpp`: Reader view rendering, menu behavior, generic status rendering, Reader and Books image modes, control feedback ownership, refresh policy, and PaperMono frame contents.
- `firmware/main/gray4_tests.cpp`: unified 2bpp packing, pixel bounds, four-level quantization, deterministic 1-bit Bayer projection, intermediate-gray detection, and SSD1677 mono/Gray4 RAM encoding.
- `run_tests.py`: optional ESP-IDF build, QEMU flash-image assembly, QEMU execution, log monitoring, and result reporting.

## Environment and dependencies

- ESP-IDF 5.5.x with the `esp32s3` toolchain and Python environment activated. `IDF_PATH` must be set when using `--build`.
- `esptool` installed in the active Python environment.
- Espressif `qemu-system-xtensa`, either on `PATH`, under `IDF_TOOLS_PATH/qemu-xtensa`, or named by `QEMU_SYSTEM_XTENSA`.

No fixed working directory is required because the runner resolves paths from its own location. From the repository root, run:

```text
python ci/reader_tests/run_tests.py --build
```

To rerun an existing test build:

```text
python ci/reader_tests/run_tests.py
```

The run succeeds with exit code `0` after printing `ALL_READER_TESTS_PASSED`. A build error, QEMU termination, `TEST_FAILURE`, Guru Meditation error, or timeout produces a nonzero exit. `--timeout SECONDS` changes the default 240-second QEMU limit.

The runner generates `firmware/build/`, `firmware/sdkconfig`, `test_build.log`, `qemu.log`, and `qemu_flash.bin`. These files remain inside `ci/reader_tests` and are ignored by Git.

---

# Reader 本地测试

## 用途

`reader_tests` 是在本地运行的 ESP32-S3 QEMU 集成测试工程。它编译生产代码中的 App Runtime、Launcher、Books catalog 与书架、Reader、存储、书籍服务、EPUB 和 PaperMono UI，并用可控的存储及显示适配器替代硬件。

测试源码的职责如下：

- `firmware/main/test_main.cpp`：测试 Launcher/Books/Menu 返回栈、App 切换和通用启动参数、BookCatalogService 发现与持久化、Books/Reader 集成、TXT/EPUB 阅读流程、进度恢复、封面第 0 页导航、SD 卡生命周期、请求超时和帧所有权。
- `firmware/main/books_ui_tests.cpp`：测试一级与二级入口 descriptor、catalog 扫描过滤和排序、Books 三列布局与分页、设置事务状态、modal 输入截获、TXT 预览和两行文件名格式化。
- `firmware/main/book_tests.cpp`：测试书籍格式识别、进度记录、页索引记录、索引构建和过期记录拒绝。
- `firmware/main/epub_tests.cpp`：测试 EPUB ZIP/container/OPF 解析、spine 正文提取、封面提取、缓存生成、资源限制以及非法或不支持输入的处理。
- `firmware/main/reader_ui_tests.cpp`：测试 Reader 视图、顶部菜单、通用状态栏、Reader/Books 图片模式、控件反馈所有权、刷新策略和 PaperMono 帧内容。
- `firmware/main/gray4_tests.cpp`：测试统一 2bpp 打包、像素边界、四档量化、确定性 1-bit Bayer 投影、中间灰阶检测以及 SSD1677 单色/四灰度 RAM 编码。
- `run_tests.py`：按需执行 ESP-IDF 构建，合成 QEMU Flash 镜像，启动 QEMU，监控日志并判断结果。

## 运行环境和依赖

- 已激活带有 `esp32s3` 工具链和 Python 环境的 ESP-IDF 5.5.x。使用 `--build` 时必须设置 `IDF_PATH`。
- 当前 Python 环境中已安装 `esptool`。
- Espressif `qemu-system-xtensa` 位于 `PATH`、`IDF_TOOLS_PATH/qemu-xtensa` 下，或通过 `QEMU_SYSTEM_XTENSA` 指定完整路径。

脚本根据自身位置解析路径，因此不强制要求工作目录。从仓库根目录执行：

```text
python ci/reader_tests/run_tests.py --build
```

复用已有测试构建时执行：

```text
python ci/reader_tests/run_tests.py
```

测试打印 `ALL_READER_TESTS_PASSED` 后以状态码 `0` 成功结束。构建错误、QEMU 提前退出、`TEST_FAILURE`、Guru Meditation 错误或超时都会产生非零状态码。`--timeout SECONDS` 可修改默认的 240 秒 QEMU 时限。

脚本会生成 `firmware/build/`、`firmware/sdkconfig`、`test_build.log`、`qemu.log` 和 `qemu_flash.bin`。这些文件只保存在 `ci/reader_tests` 内，并被 Git 忽略。
