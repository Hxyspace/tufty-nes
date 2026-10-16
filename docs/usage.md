# 使用指南

## USB 接口

普通模式提供两个串口：

| 接口 | 名称 | 用途 |
|---|---|---|
| CDC0 | Console | 查看状态、修改设置、进入磁盘或 BOOTSEL 模式 |
| CDC1 | Gamepad | 接收手柄协议，由 PC 发送器使用 |

Console 设置：115200 波特率、关闭本地回显、启用 DTR。连接后输入 `help` 查看命令。支持退格和 Ctrl+C 清除输入。

普通模式 USB VID/PID 为 `CAFE:4025`，磁盘模式为 `CAFE:4027`。切换后串口编号可能改变，重新连接时请按接口名称选择。

## Console 命令

| 命令 | 用途 |
|---|---|
| `help` | 显示命令列表 |
| `status` | 查看文件系统、游戏数量及手柄状态 |
| `brightness 1..100` | 调整屏幕亮度 |
| `padid 1..65535` | 设置手柄 ID，默认 1 |
| `swapab 0/1` | 关闭或启用 A/B 互换 |
| `save` | 保存配置 |
| `rescan` | 返回菜单并重新扫描游戏 |
| `usb disk` | 进入 USB 磁盘模式 |
| `bootsel` | 进入刷机模式 |

修改设置后执行 `save`。启动不同游戏、通过命令进入磁盘模式前也会保存配置；保存失败时会显示错误。`usb disk`、`bootsel` 会先回复状态再切换。

## PC 手柄

Windows：

```powershell
python -m pip install pyserial
python tools/serial_gamepad.py
python tools/serial_gamepad.py --port COM7 --controller-id 1
```

设备 `padid` 必须与发送器的 `--controller-id` 相同。默认使用 TGP v2 提供 NES 八键；旧版五键发送器可用 `--v1`，仅支持方向和 NES A。

发送器读取全局键盘状态，运行时其他窗口也可能收到按键。Ctrl+C 释放按键并退出。完整键位见 [README](../README.md#操作)。

Linux 暂只支持发送固定按键状态：

```bash
python3 tools/serial_gamepad.py --port /dev/ttyACM1 --buttons 0x80 --seconds 3
```

## 管理游戏

通过 Console 的 `usb disk` 或连续双 Reset 进入磁盘模式，把游戏放到盘根目录 `roms/`。完成后使用电脑的安全弹出操作，设备自动返回菜单。磁盘模式下暂停游戏、手柄和 Console 命令。

游戏文件要求：

- 标准 iNES 1.0 `.nes` 文件，单个文件最多 4 MiB。
- 只扫描 `roms/` 第一层，最多 128 个游戏。
- 文件名最多 255 个 UTF-8 字节，菜单按文件名排序。
- 当前字体只显示 ASCII，建议使用英文文件名及两位数字前缀。

重命名游戏后，请删除设备上的旧文件以免重复显示；上次选择会在启动新文件后更新。

## ROM 检查与修复

将电脑上的游戏放到仓库根目录 `roms/`，执行：

```bash
python3 tools/deps.py --profile host
python3 tools/prepare_roms.py
python3 tools/prepare_roms.py --fix
python3 tools/prepare_roms.py --fix --trim-padding
```

默认只检查。`--fix` 按已知校验值修正头部，修改前将完整原文件备份到 `build/rom-backups/`；用 `--backup-dir` 可更换备份位置。`--trim-padding` 额外清理声明 ROM 数据之外的 ASCII 空白尾部。处理完成后，将游戏手动复制到设备。

检查项包括格式、文件长度、mapper、大小限制和尾部数据。未知 ROM 不修改头部，截断或不支持的游戏会报错。修正规则位于 `tools/rom_fixes.json`，可用 `--rules` 指定自定义规则。规则只能处理已知文件问题，不能保证所有游戏兼容。

## 封面和背景

素材使用无头、小端 RGB565 格式，放在设备磁盘内：

| 路径 | 尺寸 | 大小 |
|---|---|---|
| `roms/Game.rgb565` | 60 × 72 | 8640 字节 |
| `assets/nes-background.rgb565` | 320 × 200 | 128000 字节 |

封面应与游戏使用相同文件名主干，如 `01_Game.nes` 和 `01_Game.rgb565`。无封面时显示默认卡带；无自定义背景时依次使用 `/rom/nes-background.rgb565` 或内置背景。
