# EPUB 阅读器工作方式

本文说明源码中 EPUB 阅读链路的实际实现。Reader 支持扩展名不区分大小写的 `.epub` 文件，并将其作为无 DRM、可流式排版的 EPUB2 或 EPUB3 处理。

## 打开与数据流

1. `FileApp` 使用 `book_file_format_from_name()` 识别 `.txt` 和 `.epub`。它只建立完整逻辑路径，将路径、`media_generation` 和格式写入 `app_launch_context`，再请求打开 `ReaderApp`。
2. `ReaderApp::on_open()` 建立新的 `session_id`，取得共享文字布局，并向 `BookService` 提交 EPUB 打开命令。ReaderApp 不读取 ZIP、XML 或 FATFS。
3. BookService 的 `book_worker` 打开 ZIP，解析 `META-INF/container.xml` 中的 OPF 路径，解析 OPF 的 manifest、spine 和封面项，然后按 spine 顺序处理 XHTML。
4. XHTML 经流式过滤器转换为连续 UTF-8 文本。转换结果、spine 映射和封面以派生文件形式写入 SD 卡，完整 metadata 最后原子替换。
5. 派生正文准备完成后，现有 `book_index_engine` 对它建立分页索引。ReaderApp 仍使用共享 `reader_paginator` 生成当前页，UI 仍接收同一个 `reader_view_state` 和 `reader_page`。
6. ReaderApp 通过 BookService 的 2048 字节结果块读取派生正文或封面。队列只传递 `result_handle`；ReaderApp 在事件回调期间同步消费结果，并校验 `session_id`、`request_id`、`media_generation` 和偏移。

完整正文调用链为：

```text
FileApp
  → app_request_open_reader
  → app_launch_context
  → ReaderApp
  → BookService / book_worker
  → epub_zip_archive
  → container.xml / OPF / manifest / spine
  → epub_xhtml_filter
  → SD 派生正文
  → book_index_engine + reader_paginator
  → reader_page
  → UI frame pool
  → PaperMono renderer
```

## 模块边界

| 层级 | 模块 | 职责 |
| --- | --- | --- |
| Core | `book_file_format`、`book_types`、`text_paginator` | 定义书籍格式、跨层小型事件、EPUB 位置和值类型，并提供 TXT/EPUB 共用分页。 |
| App | `FileApp` | 识别文件格式，传递路径、卡代次和格式。 |
| App | `ReaderApp` | 管理加载、封面、正文、翻页、菜单、错误和异步结果生命周期。 |
| Service | `book_service` | 串行执行解析、缓存、分页索引和内容块读取；发布小型事件及结果句柄。 |
| Service | `epub_archive` | 校验 ZIP32 目录和本地头，查找 entry，并流式处理 Stored 或 Deflate 数据及 CRC。 |
| Service | `epub_format` | 解析 container/OPF、规范化内部路径、提取封面引用、把 XHTML 转为 UTF-8 文本、编解码 EPUB metadata 和 spine map。 |
| Service | `epub_cache_engine` | 协调解析状态机和 SD 派生文件，校验源文件及缓存，保存 EPUB 位置。 |
| UI | `reader_cover`、`reader_renderer` | 在 PSRAM 中管理有代次和引用计数的压缩封面，校验图片头，并绘制封面、正文和顶部菜单。 |
| HAL | Storage / Display | 按偏移读写普通文件；使用 M5GFX 将 JPEG/PNG 按比例居中绘入一位画布。HAL 不解析 EPUB。 |

TXT 的正文仍由 StorageService 按块读取。EPUB 的 ZIP、package、XHTML 和 SD 派生缓存位于独立模块；两种格式共享 ReaderApp 的交互状态、TextPaginator、页面数据、BookIndex 基础机制、UI frame 和 Renderer。

## ZIP、package 与路径

ZIP 读取使用中央目录定位 entry，并在提取时再次核对本地文件头、文件名、方法、标志、大小和 CRC。支持 Stored 和原始 Deflate；Deflate 使用 ESP-IDF `esp_rom` 中 Apache-2.0 许可的 miniz `tinfl`，采用 32 KiB 字典和 2048 字节输入/交付块。加密 entry、多磁盘 ZIP、ZIP64 尺寸和其他压缩方法会被拒绝。

`container.xml` 提供 OPF rootfile。OPF parser 读取 manifest 和线性 spine，支持默认命名空间或带前缀的名称。内部引用先移除 query/fragment、解码百分号序列，再以引用文件所在目录为基准处理 `./`、`../` 和重复 `/`；越过归档根、反斜线、控制字符、绝对路径和外部 URI 会被拒绝。

## XHTML 转换

每个 spine entry 解压后直接送入流式 XHTML 过滤器，不把整章放入 RAM。过滤器：

- 保留 `body`、`span`、`strong`、`em` 等元素中的文字；
- 在 `p`、`div`、`h1` 至 `h6`、`li`、`blockquote`、`section` 和 `article` 的结束位置插入换行；
- 将 `br` 转为换行并归一化普通空白；
- 忽略标签、XML declaration、注释以及 `script`、`style` 内容；
- 解码 `amp`、`lt`、`gt`、`quot`、`apos`、`nbsp`、`ndash`、`mdash`、`hellip` 和十进制/十六进制数字实体；
- 对跨块 UTF-8 序列继续使用项目的 UTF-8 解码规则，非法序列使正文解析失败。

各 spine 的文本依次写入同一个 `epub_content.txt`。章节起点另存于 `epub_spine.map`，因此正文翻页是连续的，章节边界不改变 Reader 的上一页和下一页行为。

## SD 派生文件与缓存复用

Book ID 是规范化完整路径的 SHA-256，因此不同目录中的同名书籍使用不同目录。EPUB 文件保存在：

```text
/.system/books/<book_id>/
  epub_metadata.json
  epub_content.txt
  epub_spine.map
  epub_cover.bin        # 存在可用封面时
  metadata.json         # 共享分页索引 metadata
  pages.idx             # 共享分页索引
```

初次解析先写 `.tmp` 文件，正文、spine map 和封面完成后依次替换目标文件，`epub_metadata.json` 最后替换。没有完整 metadata 的临时产物不会被复用。原 EPUB 不会整体装入 RAM，也不会整体解包到目录；SD 上只保存连续正文、位置映射、压缩封面和索引等阅读所需派生数据。

每次打开都会核对规范化路径、文件大小、mtime 和头部/中部/尾部各 4 KiB 的 CRC32 指纹，并验证 parser/pagination 版本、派生文件大小、spine map 头及 CRC。mtime 变化但指纹相同会更新 metadata 并复用缓存；指纹、文件大小或版本不匹配会重新解析。重新解析时 EPUB 进度置为第一页，共享分页索引也从零重建，不读取旧页码或旧偏移。

## 阅读进度和索引

`epub_metadata.json` 的进度包含：

```text
spine_index
content_offset
linear_offset
```

`linear_offset` 是派生连续正文中的页首偏移；保存时通过 `epub_spine.map` 转换为 `spine_index + content_offset`，读取缓存时重新计算并核对三者一致性。Reader 正常关闭时将已完成页面的页首提交给 BookService。`pagination_version` 不一致时进度归零并重建分页索引。

共享 `book_index_engine` 在 `epub_content.txt` 上生成 `pages.idx`，使页号查询、跨章节上一页和总页数沿用 TXT 的实现。第一页没有保存位置时，Reader 在可用封面存在的情况下先显示封面；点击右侧进入正文第一页。保存位置大于零时直接读取该位置，不重复显示封面。

## 封面

EPUB3 封面来自 manifest 的 `properties="cover-image"`。EPUB2 支持 metadata 中的 `meta name="cover"`，也支持 cover XHTML 或 guide 引用中的 `img` / SVG `image` 链接。实际图片仅接受 JPEG 和 PNG。

压缩封面先流式复制到 SD 的 `epub_cover.bin`，显示时再由 BookService 以 2048 字节块传递。Reader cover store 在 PaperMono 配置中只从 PSRAM 分配，最多保留两个带引用计数的压缩数据槽，每个封面上限 512 KiB；退出 Reader 时释放。PNG 必须具有有效 IHDR，JPEG 必须在前 64 KiB 内出现 SOF；宽、高各不超过 4096。M5GFX 按内容区等比例缩小并居中绘制到一位画布，不覆盖公共状态栏。

封面 entry 的解压、CRC、SD 写入、头部校验或图片解码失败时，Reader 保持正文可读：解析阶段丢弃封面，显示阶段无法使用封面时进入正文或显示封面占位内容，右侧仍可进入正文。

## 任务、内存和生命周期

EPUB 没有新增 FreeRTOS task。原 BookIndex worker 扩展并命名为 `book_worker`，stack 为 12288 字节、priority 为 2，串行处理 BookService 命令、EPUB 状态机和分页索引。每个循环只执行一个有界步骤并 `vTaskDelay(1)`，Mooncake 的 `on_open()`、`on_running()` 和事件回调不做 ZIP/XML/XHTML 扫描或图片解码。封面解码发生在 UI renderer task。

固定或明确受限的容量如下：

| 项目 | 上限 |
| --- | ---: |
| ZIP entry | 512 |
| EPUB 内部路径 | 256 字节加结尾空字符 |
| `container.xml` | 64 KiB |
| OPF | 256 KiB |
| manifest item | 128 |
| spine item | 128 |
| 单个 spine XHTML 解压大小 | 8 MiB |
| 派生连续正文 | 64 MiB |
| XHTML/XML token | 256 字节 |
| ZIP / BookService 数据块 | 2048 字节 |
| Deflate 字典 | 32 KiB |
| 压缩封面 | 512 KiB |
| 封面宽或高 | 4096 像素 |
| BookService 内容结果池 | 2 槽 |

ZIP entry 表、XML 缓冲、manifest/spine 表和 Deflate 字典使用显式定额分配；PaperMono 构建将这些解析期大对象放在 PSRAM。分配失败返回错误，不通过 C++ 容器扩容。整本派生正文和分页索引始终留在 SD 卡。

SD 状态由 `media_generation` 校验。拔卡或换卡后，worker 的后续读写失败；旧 session、旧 request、旧 generation 和已释放 handle 不会被 Reader 接受。

## 支持边界

当前处理 EPUB2/EPUB3 中无 DRM、可流式排版、ZIP32、Stored/Deflate、线性 spine、UTF-8 XHTML 纯文字以及 JPEG/PNG 封面。

以下内容不进入当前阅读模型：目录 UI、章节选择、搜索、书签、批注、字体/字号/行距设置、CSS 布局、正文图片、表格、正文 SVG、音视频、JavaScript、DRM、加密 entry、Fixed Layout、Media Overlay、外部链接、网络资源和 OPDS。非线性 spine item 会跳过。字体未覆盖的码点继续使用现有字形回退。

已知兼容性边界包括 ZIP64、多磁盘 ZIP、非 Stored/Deflate 压缩、超过上述容量的 package/entry/path、SOF 位于 JPEG 前 64 KiB 之后，以及依赖 CSS 或浏览器布局才能表达正文结构的 EPUB。源文件指纹抽样检查头部、中部和尾部各 4 KiB；当文件大小和 mtime 同时保持不变时，完全位于三个抽样区之外的内容变化不会触发缓存重建。解析错误显示为无效 EPUB 或不支持格式；SD I/O 和卡代次错误显示存储错误。

源码入口：[文件识别](../core/book_file_format.cpp)、[ReaderApp](../app/reader/reader_app.cpp)、[BookService](../services/book/book_service.cpp)、[ZIP](../services/book/epub_archive.cpp)、[package/XHTML/metadata](../services/book/epub_format.cpp)、[SD 缓存](../services/book/epub_cache_engine.cpp)、[封面存储](../ui/paper_mono/reader_cover.cpp)、[Reader renderer](../ui/paper_mono/views/reader_renderer.cpp)。
