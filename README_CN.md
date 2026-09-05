# SeeSeeBook

[English documentation](README.md)

## 项目介绍

SeeSeeBook 是面向 M5Stack PaperMono（ESP32-S3）的固件，基于 ESP-IDF 和 Mooncake 开发。项目在单色墨水屏上提供触摸操作的实用功能，包括菜单导航、屏幕与前光控制、RTC 设置、电池信息、SD 卡目录浏览以及 TXT/EPUB 阅读。

## 项目状态

v0.1 仍处于早期开发阶段，可能存在不稳定行为、错误或兼容性问题。本仓库的设备实现面向 PaperMono。简要版本记录见 [CHANGELOG.md](CHANGELOG.md)。

## 应用功能

| 注册 App | 菜单名称 | 用途 |
| --- | --- | --- |
| MenuApp | — | 开机进入，提供应用入口。 |
| TestApp | Screen Setting | 测试显示内容、触摸坐标和持续时间（含长按）；提供 OFF、25%、50%、75%、100% 五档前光调节。 |
| RTCSettingApp | RTC Setting | 读取并使用数字键盘编辑本地 RTC 日期和时间，保存前进行校验。 |
| BatteryApp | Battery | 显示电量百分比、电压、电流栏和充电状态；页面打开时每 5 秒采样一次。 |
| FileApp | Files | 浏览 SD 卡目录和分页文件列表，打开支持的书籍文件，并处理插卡与拔卡状态。 |
| ReaderApp | — | 阅读 UTF-8 TXT 和无 DRM 的流式排版 EPUB，提供翻页和 SD 卡进度保存。 |

点击菜单入口打开 App，使用 `< Back` 返回。公共底部状态栏显示 `HH:MM`、电量百分比，并在确认充电时显示闪电符号。PaperMono 的电流读数不可用，以 `--` 显示；充电状态不可用时显示 `Unknown`。BatteryApp 仅监测信息，不配置充电参数。

- RTC Setting 在进入时读取一次 RTC。点击日期或时间字段后输入数字，点击勾号保存；退格清空所选字段，Back 取消未保存的编辑。所有数值使用设备本地时间，不做时区转换。
- Files 使用 FAT32 SD 卡，不自动格式化。目录优先于文件，按名称排序。点击目录进入，使用 `..` 返回上级（在 `/` 下无动作），使用底部箭头翻页。支持长文件名和中文文件名；名称单行显示，超出部分以 `...` 代替。点击 `.txt` 或 `.epub` 文件会进入 Reader，其他文件显示三秒的不支持提示。挂载出错后需拔出并重新插入 SD 卡。
- Reader 的正文左区和右区用于翻页，中区用于打开顶部菜单；菜单返回控件显示为 `<`。EPUB 在没有保存位置且存在可用封面时先显示封面，已有进度时直接恢复保存页。实现细节见 [TXT 阅读器说明](Docs/txt_reader.md)和 [EPUB 阅读器说明](Docs/epub_reader.md)。

## 项目架构

工程按独立的 ESP-IDF 组件组织，通过各层的 `CMakeLists.txt` 配置构建。

```text
App 逻辑 (app/)
    |
    v
服务 / 系统运行时 (services/, system/)
    |
    v
硬件抽象层 (m5_hal/) --> PaperMono

App --> View State --> UI Renderer (ui/) --> Display HAL
```

- `app/` 负责应用逻辑和状态，并接入 Mooncake 生命周期。
- `services/` 提供 RTC、电池、存储、输入、前光、书籍索引和 EPUB 解析/缓存能力；`system/` 统一协调运行时更新、事件分发和公共状态。
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

## 二次开发

### 添加 App

1. 在 `app/<app_name>/` 中添加继承自 [app_base](app/include/app/app_base.hpp) 的应用逻辑，并在 `ui/include/ui/` 中定义 View State。
2. 在 `ui/paper_mono/views/` 中添加或复用 Renderer，布局放在 `ui/paper_mono/layout.hpp`，接入已有的 UI 呈现与交互接口。
3. 添加 App 和 View 标识，在 [app/app_registry.cpp](app/app_registry.cpp) 中注册描述符（`kind`、`view`、`name`）与工厂函数。需要菜单入口时，在 [app/menu/menu_layout.hpp](app/menu/menu_layout.hpp) 中添加目标和名称；数组顺序决定显示顺序。菜单容量为四项，超出会在编译时检查失败。可按需增加容量。
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
