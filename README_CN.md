# SeeSeeBook

[English documentation](README.md)

> 这是一个大学生的练手作品，由作者与 Codex 共同完成，其中可能存在一些不成熟之处。如有任何问题或建议，欢迎交流。

## 项目介绍

SeeSeeBook 是面向 M5Stack PaperMono（ESP32-S3）的固件，基于 ESP-IDF 和 Mooncake 开发。项目在 PaperMono 触摸屏上提供基于 SD 卡的 Books 书架、文件浏览、TXT/EPUB 阅读、设备设置和诊断功能。

## 项目状态

当前版本为 V0.2，目标设备为 M5Stack PaperMono。书架目录、EPUB 派生数据、分页索引和阅读进度统一保存在 SD 卡的 `/.system/books/` 下。版本摘要见 [CHANGELOG.md](CHANGELOG.md)，存储格式和实现边界见阅读器说明文档。

## 应用功能

| 注册 App | 入口 | 用途 |
| --- | --- | --- |
| LauncherApp | 启动页 | 固定显示一级入口 `Books`、`File`、`Menu`。 |
| BooksApp | Books | 以 3 × 2 网格显示 SD 书架，管理 TXT/EPUB 扫描设置、TXT 预览和 EPUB 书架封面，并打开 Reader。 |
| FileApp | File | 浏览 SD 卡目录和分页文件列表，打开支持的书籍，并处理插卡与拔卡状态。 |
| MenuApp | Menu | 按 descriptor 顺序显示 `Screen Setting`、`RTC Setting`、`Battery`、`Gray4 Test`。 |
| TestApp | Screen Setting | 显示屏幕和触摸诊断信息，并提供 OFF、25%、50%、75%、100% 五档前光调节。 |
| RTCSettingApp | RTC Setting | 读取并使用经过校验的数字键盘编辑本地 RTC 日期和时间。 |
| BatteryApp | Battery | 显示电量百分比、电压、电流可用状态和充电状态；页面打开时每 5 秒采样一次。 |
| Gray4TestApp | Gray4 Test | 显示四个固定灰阶块和分段灰阶带，用于 PaperMono 实机检查。 |
| ReaderApp | 内部入口 | 阅读 UTF-8 TXT 和无加密的流式 EPUB，支持分页、EPUB 逻辑第 0 页封面和 SD 进度保存。 |

公共底部状态栏显示 `HH:MM`、电量百分比，并在确认充电时显示闪电符号；中部可显示 Books 或 Reader 页码。当前 PaperMono 实现不能取得电流读数，因此 BatteryApp 的电流栏显示 `--`；充电状态不可用时显示 `Unknown`。

- RTC Setting 在进入时读取一次 RTC。点击日期或时间字段后输入数字，点击勾号保存；退格清空所选字段，Back 取消未保存的编辑。所有数值使用设备本地时间，不做时区转换。
- Books 根据独立保存的 TXT 和 EPUB checkbox 从 SD 根目录扫描，排除包含隐藏路径分量的目录。目录和设置通过临时文件原子写入 `/.system/books/catalog_v1.bin`。每页显示六本书；TXT 显示有界正文预览，EPUB 在可用时显示缓存封面。书架封面使用稳定的 1-bit Bayer 投影，以保持低成本单色刷新。
- File 使用 FAT32 SD 卡，不自动格式化。目录优先于文件并按名称排序。点击目录进入，使用 `..` 返回上级，使用底部箭头翻页。长文件名和中文文件名按 UTF-8 安全截断。点击 `.txt` 或 `.epub` 文件进入 Reader，其他文件显示临时的不支持提示。
- Reader 正文左区和右区用于翻页，中区用于打开顶部菜单；菜单返回控件为 `<`。可用 EPUB 封面是逻辑第 0 页，此时隐藏页码。已保存书籍恢复到经过校验的页面；分页数据不兼容时完全弃用旧进度和索引并从头重建。实现细节见 [TXT 阅读器说明](Docs/txt_reader_CN.md)和 [EPUB 阅读器说明](Docs/epub_reader_CN.md)。

## 书籍格式支持边界

| 格式 | 已实现范围 | 边界 |
| --- | --- | --- |
| TXT | UTF-8，可带 BOM；支持 LF、CRLF、CR 换行，以及按需分页、页索引和进度恢复。 | 不解码其他文本编码；非法 UTF-8 会进入 Reader 错误状态。 |
| EPUB | 无加密的流式 EPUB2/EPUB3；支持 ZIP32 Stored/Deflate、线性 spine、UTF-8 XHTML 正文和 JPEG/PNG 封面。 | 不处理 DRM、加密 entry、ZIP64、固定版式、CSS/浏览器排版和正文多媒体。 |

解析、存储、进度和容量限制详见 [TXT 阅读器说明](Docs/txt_reader_CN.md)与 [EPUB 阅读器说明](Docs/epub_reader_CN.md)。

## PaperMono 显示

所有绘制统一存入 PSRAM 中一张 800 × 480 packed 2bpp framebuffer。纯黑白 UI 帧使用单色 OTP 路径，Renderer 根据残影债务阈值执行单色清理；包含浅灰或深灰的帧使用 SSD1677 `0xD7` 四灰度全刷。Reader 封面和 Gray4 Test 使用真实四灰度，Books 书架封面明确使用 1-bit 抖动。单色全刷或四灰度全刷期间前光会关闭，完成后恢复。

PaperMono 的 `0xFF` 快速刷新在激活前把完整的 48,000 字节单色目标同步到控制器 RAM1。UI dirty rect 仍限制逻辑绘制、请求合并和分区残影计数，但不裁剪这次控制器传输。驱动细节和参考来源见 [PaperMono 显示后端说明](m5_hal/paper_mono/display/README.md)。

## 项目架构

工程按独立的 ESP-IDF 组件组织，通过各层的 `CMakeLists.txt` 配置构建。

```text
Launcher / Books / File / Menu / Reader (app/)
    |
    v
系统运行时与服务 (system/, services/)
    |                |
    |                +--> SD catalog / EPUB cache / 索引 / 进度
    |
    v
硬件抽象层 (m5_hal/) --> PaperMono

App --> View State --> UI Renderer (ui/) --> Display HAL
```

- `app/` 负责应用逻辑和状态，并接入 Mooncake 生命周期。
- `services/` 提供 RTC、电池、存储、输入、前光、书架目录、书籍索引和 EPUB 解析/缓存能力；`system/` 统一协调初始化、运行时更新、事件分发和公共状态。
- `ui/include/ui/` 定义 View State 和 UI 接口；`ui/paper_mono/` 负责设备相关的绘制与布局。
- `m5_hal/include/hal/` 定义硬件接口；`m5_hal/paper_mono/` 实现 PaperMono 硬件访问。
- `main/` 初始化并运行系统；`core/` 保存公共数据契约和[项目关键信息](core/include/core/project_info.hpp)。

通用 App 逻辑和 Service 与设备专属的 HAL、Renderer 实现分离。

## 快速开始

### 准备依赖

请先准备 Python 3，并确保 Git 位于 `PATH`。在项目根目录运行共用的 Python 脚本 [scripts/fetch_dependencies.py](scripts/fetch_dependencies.py)。

执行前注意：脚本可能拉取并将已有的干净依赖仓库切换到配置指定的 tag。遇到本地改动、origin 不匹配，或目标目录不是 Git 仓库时会停止，不会覆盖这些目录。

Windows（`python` 指向 Python 3）：

```powershell
python scripts/fetch_dependencies.py
```

Unix-like 系统（`python3` 指向 Python 3）：

```bash
python3 scripts/fetch_dependencies.py
```

脚本读取 [dependencies.json](dependencies.json)，其中列出的依赖为：

| 依赖 | 用途 | 来源 / tag | 目标目录 |
| --- | --- | --- | --- |
| Mooncake | App 生命周期与应用切换框架 | [Forairaaaaa/mooncake](https://github.com/Forairaaaaa/mooncake)，`v2.3.3` | `dependencies/mooncake/` |

- 检查 Git 和 JSON 配置，不安装任何软件。
- 自动创建缺失的 `dependencies/`，按配置 tag 浅克隆缺失的仓库，并核对 origin 和检出的提交。
- 复用版本匹配的干净仓库；否则拉取指定 tag，并以 detached 模式检出。发生错误时停止。
- 仅在配置启用时初始化子模块；Mooncake 的此选项为关闭。

在上述任一命令后添加 `--check`，可仅检查 Git 和配置而不下载。M5Unified 由组件 manifest 单独声明，通过 ESP-IDF Component Manager 解析，不由该脚本下载。[dependencies.lock](dependencies.lock) 记录了 M5Unified 0.2.21 及其依赖 M5GFX 0.2.28。

### 使用 ESP-IDF v5.5.5 编译

准备好依赖后，激活 ESP-IDF v5.5.5 环境及其 Python 虚拟环境。在项目根目录依次运行：

```bash
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

- `set-target esp32s3`：将目标芯片设置为 ESP32-S3。
- `reconfigure`：重新生成并检查工程配置与组件依赖。
- `build`：编译项目并生成固件构建产物。

### 运行本地集成测试

测试固件在 ESP32-S3 QEMU 中验证 App 导航、Books catalog、TXT/EPUB 阅读、SD 生命周期、进度和索引、UI frame、Gray4 打包、图片投影与刷新策略。在 ESP-IDF 和 QEMU 工具可用时执行：

```bash
python ci/reader_tests/run_tests.py --build
```

成功运行会以状态码 `0` 退出，并输出 `ALL_READER_TESTS_PASSED`。工具查找、复用已有构建、生成文件和失败条件见 [ci/README.md](ci/README.md)。PaperMono 波形、残影、前光时序和物理灰阶区分仍需在设备上观察。

## 二次开发

### 添加 App

1. 在 `app/<app_name>/` 中添加继承自 [app_base](app/include/app/app_base.hpp) 的应用逻辑，并在 `ui/include/ui/` 中定义 View State。
2. 在 `ui/paper_mono/views/` 中添加或复用 Renderer，布局放在 `ui/paper_mono/layout.hpp`，接入已有的 UI 呈现与交互接口。
3. 添加 App 和 View 标识，在 [app/app_registry.cpp](app/app_registry.cpp) 中注册描述符（`kind`、`view`、`name`）与工厂函数。一级入口定义在 [app/launcher/launcher_layout.hpp](app/launcher/launcher_layout.hpp)，二级入口定义在 [app/menu/menu_layout.hpp](app/menu/menu_layout.hpp)。数组顺序决定显示顺序，编译期检查会拒绝重复、未注册或超过容量的配置。
4. 将源文件加入对应组件的 `CMakeLists.txt`。复用现有事件分发和非阻塞的 Mooncake 生命周期回调，通过 Service 使用系统能力，不在 App 中直接访问 HAL。

### 添加设备

1. 在 `m5_hal/<device>/` 中实现公共硬件接口，可参考已有的 `m5_hal/paper_mono/`。
2. 在 `ui/<device>/` 中实现或适配 Renderer 和布局。
3. 复用通用 App、Service 和事件接口，避免在这些层引入设备专属依赖。
4. 更新必要的组件与构建配置，并补充支持设备说明。

## 参考项目与依赖

- [ESP-IDF v5.5.5](https://github.com/espressif/esp-idf/tree/v5.5.5)：SeeSeeBook 使用的固件开发框架与构建系统。
- [M5Unified](https://github.com/m5stack/M5Unified)：由设备 HAL 使用，提供 PaperMono 外设访问能力。
- [Mooncake v2.3.3](https://github.com/Forairaaaaa/mooncake/tree/v2.3.3)：应用生命周期与应用切换框架。
- [M5PaperMono-OTP-Demo](https://github.com/m5stack/M5PaperMono-OTP-Demo)：PaperMono 墨水屏刷新后端的参考项目，来源说明见[驱动文档](m5_hal/paper_mono/display/README.md)。
- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)：一款优秀的开源阅读器固件，也是 SeeSeeBook 的灵感来源。使用它之后，作者萌生了自己动手开发阅读器固件的想法。

列出这些项目不表示存在官方隶属或背书关系。

## 许可证

SeeSeeBook 采用 [MIT 许可证](LICENSE)。第三方依赖和移植代码保留各自的许可与署名要求。
