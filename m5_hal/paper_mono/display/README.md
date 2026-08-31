# PaperMono SSD1677 OTP 显示驱动

本目录是 SeeSeeBook 的 PaperMono 私有显示后端。它使用 M5GFX 的
`M5Canvas` 生成单色离屏 framebuffer，但不使用 M5GFX 当前的
`Panel_SSD1677_4Gray` 刷新波形。SSD1677 的实际刷新由本目录中的 OTP
驱动完成。

工程根目录的 README 不包含设备驱动细节，因此驱动来源、许可和迁移约束
集中记录在此文件。

## 来源与固定参考

- 原厂示例：<https://github.com/m5stack/M5PaperMono-OTP-Demo>
- 本次移植固定参考提交：
  `c7c02554f89fd06f80d988b805b2a59050c78a46`
- PaperMono 官方文档：<https://docs.m5stack.com/en/core/PaperMono>
- 控制器：Solomon Systech SSD1677，原生 framebuffer 为 800 × 480、
  1 bit、每行 100 bytes、MSB first。

移植保留了原厂示例的 MIT SPDX 版权声明。原始项目的 MIT License 允许
使用、修改和再发布，但要求在软件副本或主要部分中保留版权及许可声明。
本目录的派生源文件均保留对应声明；完整许可文本可在原厂仓库和本工程
`LICENSE` 中查阅。

## 分层边界

```text
App / View State
        ↓
UI Renderer
        ↓  display_surface + region + logical refresh_mode
PaperMono display adapter
        ├── M5Canvas: 1-bit rasterizer only
        └── SSD1677 OTP driver
                  ↓
             SPI / IOE transport
```

- `epd_otp_transport.*` 只管理 PaperMono EPD 的 SPI2、GPIO、BUSY、M5IOE1
  供电和复位。显示 SPI 总线按唯一显示所有者模型无限等待；SSD1677 BUSY
  与内部 I²C 仍使用有限超时，防止硬件异常永久占用其他系统资源。
- `epd_otp_driver.*` 只管理 SSD1677 命令、官方 OTP 波形、RAM 基线和深睡；
  不包含 UI 残影策略或 App 状态。
- `paper_mono_display.cpp` 是设备适配层，负责把设备无关的
  `display_surface` 映射到 1-bit `M5Canvas`，再把逻辑刷新请求交给 OTP
  驱动。
- App、Service 和公共 UI 接口不包含 M5Unified、M5GFX 或 SSD1677 类型。
  迁移到其他设备时，可以替换 `m5_hal/<device>` 和对应 Renderer，而不改变
  App 的业务状态。

M5Unified 仍负责 PaperMono 设备识别、触摸、RTC、电源、前光和 M5IOE1。
初始化完成后仅释放 M5Unified 的 EPD SPI 总线，后续禁止再调用
`M5.Display.display()`、`setEpdMode()` 或 `waitDisplay()`；前光接口走独立的
I²C/Light 实现，仍由现有 HAL 使用。

## 刷新模式

原厂 OTP 示例为当前黑白 UI 提供两个经过验证的物理路径，因此公共逻辑
模式按下表映射，不自行创造未公开的 LUT：

| 公共模式 | SSD1677 实际模式 | 典型用途 |
|---|---|---|
| `fastest` | OTP 单色局刷，`0xFF` | 按下/抬起反馈、状态栏 |
| `text` | OTP 单色局刷，`0xFF` | 文件列表等文字内容变化 |
| `fast` | OTP 单色局刷，`0xFF` | 普通内容更新 |
| `quality` | OTP Mode 1 单色全刷，`0xF8` 预同步后 `0x14` | 进入/退出 App、残影清理、失败恢复 |

逻辑模式保留是为了让 App/UI 策略可迁移；在 PaperMono OTP 后端中，前三种
模式不会伪造不同物理速度。实际耗时由 HAL 日志记录，应以上板测量为准，
不能继续套用旧 M5GFX 四灰阶路径的测量值。

HAL 不按次数升级刷新模式。Renderer 按逻辑区域维护独立残影债务：普通区域
仍按 10 次 `fastest` 产生 1 次逻辑 `fast`、累计 5 次 `fast` 后请求一次
`quality`；文件文字区域和状态栏使用各自阈值。PaperMono 后端只把这三种
逻辑局刷统一映射到官方 `Partial OTP (0xFF)`，并执行 Renderer 明确请求的
`quality`。Renderer 只在硬件刷新成功后累计或清除残影债务。

局刷严格执行“完整当前帧写入 RAM1 → `0x21=0x00` → `0x22=0xFF` →
Master Activation → 等待 BUSY”的原厂顺序。激活后不再写 RAM2，也不使用
`0xD4`、`0x1C` 或其他未经该 OTP 示例验证的组合。刷新失败时基线会标记为
无效，下一次请求必须用全刷重建；UI 只额外执行一次有界的 Quality 恢复，
不会无限重试。

连续交互期间面板最多保持唤醒 400 ms。每次 Partial 都显式写入官方要求的
`0x3C=0x80`；若面板已经深睡，则先硬件复位并等待 BUSY，若仍处于唤醒状态，
则只恢复 Partial 边框状态。这避免紧接 Quality 的局刷继承 `0x3C=0x01`。

## 区域语义

公共 HAL 仍同时接收刷新模式和逻辑脏区域，区域用于边界校验、日志、UI
残影债务及以后迁移到支持硬件窗口刷新的设备。为严格保持原厂 OTP 示例的
已验证时序，当前 SSD1677 局刷会传输完整 48,000-byte 新帧，再由差分波形
只改变实际不同的像素；它不会使用未经原厂示例验证的窗口化 OTP 命令组合。

因此状态栏仍是独立的逻辑刷新区域，但底层 SPI 会发送完整单色帧。这不会让
App 或 Renderer 依赖 PaperMono 的控制器 RAM 方向。

## 迁移与验证清单

更换设备时应在新设备私有 HAL 中重新确认：

1. 原生 framebuffer 尺寸、bit order、黑白 bit 定义和画布旋转。
2. 显示总线所有权以及与触摸、SD、RTC、电源总线的关系。
3. 刷新失败后的基线恢复方式、BUSY 电平及合理超时。
4. 温度范围、区域残影债务和厂商建议的全刷清理周期。
5. 在真机上检查四角方向、触摸命中一致性、纯白背景、连续按键反馈、状态栏
   分钟刷新、FileApp 翻页以及区域债务触发的全刷。

完整编译只能验证类型、依赖和链接，不能替代真机波形与方向测试。
