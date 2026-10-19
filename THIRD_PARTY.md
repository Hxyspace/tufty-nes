# 来源与许可

本项目采用 [GPL-3.0](LICENSE)。依赖的固定版本记录在 `deps.lock.json`，适配补丁位于 `patch/`。以下组件和素材保留各自版权与许可证。

| 内容 | 来源 | 本项目使用方式 |
|---|---|---|
| InfoNES、NES mapper、FatFs R0.14b | [lalomorales22/tufty-nes](https://github.com/lalomorales22/tufty-nes)，基于 xrip/pico-nes、InfoNES | 锁定 `799ecf25022d5216773c8b80fcf995950aa70f4e`，应用接入及 PPU/mapper 48 修正补丁；上游 `pico-nes/LICENSE` 及 FatFs 源码内声明保留 |
| Pico SDK 2.3.0、TinyUSB | Raspberry Pi、TinyUSB 作者 | 锁文件下载；TinyUSB 由 SDK 的 gitlink 定版，按各自 BSD/MIT 许可分发 |
| picotool | Raspberry Pi | 锁文件下载，BSD-3-Clause，作为宿主构建工具 |
| LittleFS 2.11.3 | littlefs-project | 锁文件下载，BSD-3-Clause |
| cJSON 1.7.19 | Dave Gamble 等 | 锁文件下载，MIT |
| RM1 ROMFS 解析、文件系统布局、独占 MSC、Reset 入口 | `tufty2350-native/src/storage/`、USB 实现 | `src/rom_store.*`、存储/USB 设计；MIT 声明见 `licenses/LICENSE` |
| ST7789 初始化和 PIO 并口驱动 | native，经 Pimoroni badgeware-cpp，参考提交 `7d1ef0882c6e9dcf374542528f6f211e287b5afb` | 改写为双核 DMA 显示；见 `licenses/PIMORONI-LICENSE` |
| Tufty Reset 设计 | Pimoroni Tufty，参考提交 `ee84772fb7f7d5424e9593029caba815c6f89c80`，经 native | USB Reset 设计，Pimoroni MIT 许可同上 |
| 深度休眠、唤醒与长按 Reset 动画 | `tufty2350-native/src/platform/tufty/{powman,reset_startup}.cpp`，基于 Pimoroni Tufty POWMAN 实现 | `src/power_board.*`、`src/power_reset.cpp`；MIT 许可见 `licenses/LICENSE` 和 `licenses/PIMORONI-LICENSE` |
| Console 交互 | native 的 rtc_console、rtc_platform | 原生实现回显/换行/退格，参考工程 MIT 许可同上 |
| USB 磁盘界面、星空及背灯交互 | Pimoroni `tufty2350/modules/python/_msc.py`，参考提交 `c4abbcd32fef7aa12985635d68daed967b04f594` | `src/disk_ui.cpp` 原生适配，NES 专用文案；Pimoroni MIT 声明同上 |
| Ark 6 / Absolute 10 像素字体 | 同一 Tufty 源码的 `romfs/fonts/{ark,absolute}.ppf` | `src/disk_fonts.hpp` 保存 ASCII 字形，生成脚本保留源文件 SHA256 |
| TGP 会话与菜单设计、背景 | `TUFTY_yuan/lib/tufty_runtime/gamepad/`、`office/apps/menu/`、`office/assets/background.png` | 原生重写；本项目 TGP v3 承载共用手柄 Report，背景来自该项目素材 |
| Lato Regular/Heavy/Bold ASCII 字形 | Łukasz Dziedzic | 生成菜单位图，SIL OFL 1.1，见 `licenses/LATO-LICENSE` |

`tools/rom_fixes.json` 记录从 [FCEUX ines-correct.h](https://github.com/TASEmulators/fceux/blob/master/src/ines-correct.h) 核对的部分 CRC32/mapper/镜像事实，未引入 FCEUX 程序代码或完整数据库。Rainbow Islands 规则增加了 PRG/CHR 数据的 SHA256 和长度约束。ROM 文件不随项目或固件分发。

BLE 使用 CYW43 驱动和 BlueKitchen BTstack，版本由 SDK gitlink 锁定；安全配对还使用 BTstack 随附的 micro-ecc。分发包包含 CYW43、BTstack 原始许可、Raspberry Pi 平台授权条款及 micro-ecc 许可；这些组件各自的许可适用范围以原文为准。

`src/ble_stack.c` 的启动包装器改编自 Pico SDK `btstack_cyw43.c`，保留 Raspberry Pi BSD-3-Clause 声明，使用 RAM 配对数据库避免覆盖 State。十字节 Report 依据实体 XTARK 手柄的 Report Map 和实测通知，接入参考 `XTARKGamepad` 接收库及 Android `ble_gamepad`；未引入 ESP32/NimBLE 实现。

`tools/package.py` 将当前固件实际依赖的许可证和源码版本信息复制到 `dist/`；依赖完整来源在 `deps.lock.json`，对上游的修改在 `patch/`。
