# Tufty NES BLE

运行在 **Pimoroni Tufty 2350** 上的原生 NES 模拟器，支持从 Flash 加载游戏、卡片式游戏菜单、USB 磁盘管理、PC 串口手柄和 XTARK / 安卓 BLE 手柄。

## 功能

- 从 `/system/roms/` 加载 iNES 1.0 和兼容的 NES 2.0 游戏，无需将 ROM 编译进固件。
- 卡片式游戏菜单，支持自定义封面和背景。
- 双 USB 串口：CDC0 用于 Console，CDC1 用于手柄。
- Windows 键盘发送器，使用 TGP v3，与 BLE 共用按键 Report 和 Home 操作。
- BLE HID：适配 XTARK 实体手柄，支持左摇杆方向、L3+R3 长按返回菜单、自动重连及安卓主动关闭手柄。
- USB MSC 磁盘模式，可直接复制和管理游戏文件。
- 保存屏幕亮度、手柄 ID、按键映射和上次选择的游戏。
- 支持手动深度休眠和菜单空闲自动休眠，按实体按键唤醒。
- ROM 检查工具，支持已知错误头部修正及备份。

## 硬件与兼容性

| 项目 | 要求 |
|---|---|
| 开发板 | Pimoroni Tufty 2350 |
| MCU | RP2350B |
| 存储 | 16 MiB Flash、8 MiB PSRAM |
| 屏幕 | 320 × 240 ST7789 |
| 游戏格式 | iNES 1.0、部分 NES 2.0，最多 4 MiB/文件 |
| PC 手柄工具 | Windows、Python 3、pyserial |

设备需要预先具备兼容的 `/rom`、`/system`、`/state` 文件系统布局。本项目仅生成程序固件，不创建或格式化文件系统；布局与升级注意事项见 [存储说明](docs/storage.md)。

尚不支持声音、第二玩家、游戏存档和 VS/PlayChoice。NES 2.0 支持范围见 [游戏文件要求](docs/usage.md#管理游戏)，游戏兼容性取决于 InfoNES 的 mapper 实现。BLE 当前支持 XTARK 十字节报告布局，其他 HID 手柄需单独适配。

## 编译与刷入

安装 Linux 构建工具后，在仓库根目录执行：

```bash
sudo apt install git python3 cmake ninja-build build-essential \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
./tools/build.sh
```

首次构建会下载固定版本的依赖。生成的固件位于 `dist/tufty-nes-flash.uf2`。

1. 让设备进入 BOOTSEL 模式；运行本固件时也可在 Console 输入 `bootsel`。
2. 将 UF2 复制到出现的 RP2350 磁盘。
3. 等待设备重启进入游戏菜单。

工具版本、picotool 烧录、依赖管理和测试方法见 [构建指南](docs/building.md)。

## 添加游戏

1. 在 Console 输入 `usb disk`，或一秒内连续按两次实体 Reset。
2. 将 `.nes` 文件复制到设备磁盘的 `roms/` 目录。
3. 在电脑上安全弹出磁盘，设备会重启并显示游戏列表。

建议使用 `01_Game.nes`、`02_Game.nes` 控制菜单顺序。仓库不提供游戏 ROM，请自行准备有权使用的游戏文件。

## 操作

| 操作 | Tufty 按键 | PC 键盘 |
|---|---|---|
| 菜单上一项 / 下一项 | A 或 Up / C 或 Down | 方向键或 WASD |
| 启动游戏 | B | Z 或 Space |
| NES A / B | A / B | Z 或 Space / X |
| Start / Select | C / 由手柄提供 | Enter / 右 Shift |
| 游戏方向 | Up、Down；左右由手柄提供 | 方向键或 WASD |
| 返回菜单 | Home | Esc |
| 深度休眠 | 长按 Reset 至 LED 动画结束 | — |
| 唤醒 | A / B / C / Up / Down | — |

菜单空闲 5 分钟后自动休眠；游戏运行和 USB 磁盘模式不会自动休眠。唤醒后重新启动并返回菜单，游戏进度不保留。时间设置和串口休眠命令见 [使用指南](docs/usage.md#休眠与唤醒)。

启动 PC 手柄发送器：

```powershell
python -m pip install pyserial
python tools/serial_gamepad.py
```

自动识别失败时，用 `--port COM7` 指定 **Gamepad** 串口。设备设置请连接 **Console** 串口，使用 `help` 查看命令。

开启 XTARK BLE 手柄或安卓手柄应用的 NES / XTARK HID 模式后，Tufty 默认自动连接。十字键或左摇杆控制方向，A/B、Select/Start 控制游戏；左右摇杆回中并同时按下（L3+R3）1.5 秒返回菜单，安卓也可直接使用 Home。详见 [BLE 手柄](docs/usage.md#ble-手柄)。

串口与 BLE 同时连接时，只使用一个活跃远程手柄，本机按键始终可用。可通过 Console 分别关闭串口或 BLE，扫描设备并保存白名单，设置保存在 `/state`。详见 [连接设置](docs/usage.md#连接设置)。

## 文档

- [使用指南](docs/usage.md)：串口设置、Console、ROM 工具和菜单素材。
- [构建指南](docs/building.md)：依赖、补丁、测试和项目结构。
- [存储说明](docs/storage.md)：Flash 布局、配置保存和固件升级。
- [手柄协议](docs/gamepad-protocol.md)：设备输入、CDC1 串口和 BLE HID 接入约定。

## 来源与许可

基于 [lalomorales22/tufty-nes](https://github.com/lalomorales22/tufty-nes)、[xrip/pico-nes](https://github.com/xrip/pico-nes) 和 [InfoNES](https://github.com/jay-kumogata/InfoNES)，采用 [GPL-3.0](LICENSE)。第三方组件、素材来源和许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。
