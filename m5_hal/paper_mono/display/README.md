# PaperMono SSD1677 OTP 显示驱动

本目录是 SeeSeeBook 的 PaperMono 私有显示后端。所有文字、图形和图片先写入
同一张 2bpp 四灰度离屏画布，SSD1677 的物理刷新由本目录中的 OTP 驱动
完成。

## 固定参考

- M5Stack PaperMono OTP Demo：<https://github.com/m5stack/M5PaperMono-OTP-Demo>
- 本次移植固定提交：c7c02554f89fd06f80d988b805b2a59050c78a46
- PaperMono 官方文档：<https://docs.m5stack.com/en/core/PaperMono>
- 控制器：Solomon Systech SSD1677，原生尺寸为 800 × 480。

移植的驱动源文件保留了原厂示例的 MIT SPDX 版权声明。完整许可文本见原厂
仓库和本工程 LICENSE。

## 显示链路

    App / View State
            ↓
    PaperMono UI Renderer
            ↓  display_surface
    M5Canvas 2bpp framebuffer（PSRAM）
            ↓  扫描是否含中间灰阶
            ├── 仅黑白：现有单色 OTP 局刷或全刷
            └── 含浅灰/深灰：0xD7 四灰度 OTP 全刷
                             ↓
                    SSD1677 driver
                             ↓
                     SPI / IOE transport

epd_otp_transport.* 管理 SPI2、GPIO、BUSY、M5IOE1 供电和复位。
epd_otp_driver.* 管理 SSD1677 命令、OTP 刷新顺序、单色差分基线和深睡。
paper_mono_display.cpp 管理统一画布、坐标旋转、图像解码、灰度量化以及刷新
路径选择。App 和公共 UI 不接触 M5Unified、M5GFX 或 SSD1677 命令。

## 2bpp 像素契约

逻辑灰阶按亮度递增：

| 值 | 语义 |
|---:|---|
| 0 | Black |
| 1 | Dark Gray |
| 2 | Light Gray |
| 3 | White |

四个像素打包为一个字节，第一个像素位于 bit 7:6，第四个像素位于 bit 1:0。
原生 800 × 480 帧需要 800 × 480 × 2 / 8 = 96,000 字节，画布存放在
PSRAM。gray4_framebuffer_view 提供带尺寸与坐标检查的 pixel、fill、
fill-rect 和中间灰阶检测。中间灰阶检测逐字节执行；2-bit 像素的两位不同
时，该像素就是浅灰或深灰。

文字和普通 UI 仍使用 display_color::black/white 绘制，但它们也存入这张
2bpp 画布。图片先解码到按目标矩形申请的 8-bit 灰度临时画布，再以四个
等宽亮度区间量化后直接写入最终帧。缩放、宽高比、居中和目标矩形由
display_surface::draw_image 统一处理。

## 物理刷新选择

HAL 在每次提交前扫描完整 2bpp 帧：

| 帧内容与请求 | 实际刷新 |
|---|---|
| 仅黑白，fastest / text / fast | OTP 单色局刷，0x22=0xFF |
| 仅黑白，quality | OTP 单色全刷，0xF8 预同步后 0x14 |
| 包含浅灰或深灰，任意逻辑模式 | OTP 四灰度全刷，0x22=0xD7 |

四灰度模式将一张 2bpp 帧按行转换为两个 1-bit SSD1677 RAM 数据流，不保存
额外的全屏平面。硬件编码为 (RAM1, RAM2)：

    White=00, Light Gray=10, Dark Gray=01, Black=11

四灰度 OTP 顺序使用原厂示例的 booster、gate、边框、内部温度设置，
0x11=0x02 的 X 递减/Y 递增传输方向，以及 0xD7 Master Activation。
一行转换缓冲为 100 字节。

四灰度刷新会使单色差分基线失效。下一张纯黑白帧若请求局刷，驱动会提升为
单色全刷并重建基线；之后的纯黑白更新继续使用原有局刷。刷新失败也会使
基线失效。内部 I²C 恢复只重试一次，并保持原帧所需的灰度或单色全刷类型。

## 内存

- 持久 2bpp framebuffer：96,000 字节 PSRAM。
- 最大 480 × 760 图片解码临时画布：364,800 字节 PSRAM。
- EPUB 压缩封面槽：既有上限 524,288 字节 PSRAM。
- OTP 行转换缓冲：100 字节栈内存。
- SPI DMA staging：既有 4,092 字节内部 RAM。

显示帧、最大图片临时画布、最大压缩封面槽和 SPI staging 同时存在时合计
989,180 字节；不包含 M5GFX 对象、任务栈和其他业务数据。刷新驱动不会再
申请 48,000 字节或 96,000 字节的额外全屏平面。

## 区域与验证

公共 HAL 仍接收逻辑脏矩形。单色局刷沿用现有语义并传输完整单色帧；
四灰度 0xD7 始终执行全屏刷新。逻辑矩形继续用于边界检查、日志和 UI
残影债务。

主菜单的 Gray4 Test App 绘制四个固定灰阶块和分段灰阶带，用于实机检查
四档区分、方向、残影和刷新耗时。主机测试验证打包、边界、量化、灰阶检测
与 RAM 编码；ESP-IDF 构建只验证目标端编译和链接，物理波形效果仍需在
PaperMono 上观察。
