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
| `power status` | 查看休眠设置、唤醒原因和最近一次错误 |
| `power sleep` | 进入深度休眠 |
| `power sleep 秒数` | 定时休眠，秒数为 1–4294967 |
| `power idle 秒数` | 设置菜单空闲休眠，0 关闭，范围 0–86400 |
| `ble status` | 查看 BLE 连接、按键、报告计数、最大服务间隔/耗时及恢复次数 |
| `ble on` / `ble off` | 启用 / 关闭 BLE |
| `ble scan` | 启用 BLE 并重新连接，保留目标地址设置 |
| `ble target auto` | 自动选择广播 HID 服务且报告布局兼容的设备 |
| `ble target MAC public` | 指定 public 地址目标 |
| `ble target MAC random` | 指定 random 地址目标 |
| `ble trace on` / `ble trace off` | 开关 Report Map 和原始输入报告日志，默认关闭 |

修改设置后执行 `save`。启动不同游戏、通过命令进入磁盘模式前也会保存配置；保存失败时会显示错误。`usb disk`、`bootsel` 会先回复状态再切换。

## 休眠与唤醒

菜单默认空闲 300 秒后深度休眠。实体按键、手柄按键和 Console 输入会重新计时；手柄连接和心跳消息不影响计时。游戏运行与 USB 磁盘模式不会自动休眠，从游戏返回菜单后重新计时。

```text
power idle 600
save
```

以上设置将菜单空闲时间改为 10 分钟。使用 `power idle 0` 并执行 `save` 可关闭自动休眠。

手动休眠可长按背面 Reset，保持至 LED 动画结束（启动后约 1.64 秒）；提前松开则正常启动。双 Reset 仍进入 USB 磁盘。Reset 操作会重启设备，修改配置后应先执行 `save`。

也可在菜单或游戏运行时输入 `power sleep`，或用 `power sleep 10` 测试 10 秒后自动唤醒。串口请求和菜单自动休眠都会先保存待写配置；保存失败或唤醒输入被按住时取消休眠。状态日志发送后才关闭屏幕和 USB。

按 A/B/C/Up/Down 可唤醒，Home 不用于唤醒。入睡时没有连接 USB，也可通过插入 USB 唤醒；已连接的 USB 不会让设备立即醒来。外部 RTC 已配置的闹钟信号也可触发唤醒，本固件不设置 RTC 闹钟。

唤醒后固件重新启动并进入菜单，游戏进度不保留。电脑串口会断开，Console 和 PC 手柄发送器需要重新连接。输入 `power status` 可查看 `BUTTON`、`ALARM`、`VBUS` 等唤醒原因。

## PC 手柄

Windows：

```powershell
python -m pip install pyserial
python tools/serial_gamepad.py
python tools/serial_gamepad.py --port COM7 --controller-id 1
```

设备 `padid` 必须与发送器的 `--controller-id` 相同。默认使用 TGP v2 提供 NES 八键；旧版五键发送器可用 `--v1`，仅支持方向和 NES A。

发送器只在运行脚本的终端窗口处于前台时接收按键；切到其他窗口会自动释放全部按键，串口连接继续保持。Windows Terminal 请在前台启动脚本，建议使用独立窗口。Ctrl+C 释放按键并退出。完整键位见 [README](../README.md#操作)。`--buttons` 固定按键测试不受窗口焦点影响。

Linux 暂只支持发送固定按键状态：

```bash
python3 tools/serial_gamepad.py --port /dev/ttyACM1 --buttons 0x80 --seconds 3
```

## BLE 手柄

Tufty 作为 BLE Central 接收 HID 输入，默认自动扫描广播 `0x1812` 服务的设备。当前适配 Android 手柄的 **NES 八键 / XTARK HID** 九字节报告；TGP v1/v2 的 BLE 模式及其他 HID 布局暂不支持。

1. Android 应用中进入 **设置 → NES 八键 → NES 协议**，选择 **XTARK HID**，保存并重新开启手柄。
2. 等待 Tufty 自动连接。在 Console 输入 `ble status`，`state=READY` 表示 HID 发现完成；按键时 `buttons` 和 `reports` 应变化。
3. 用方向键切换游戏、A 启动；游戏内提供方向、A/B、Select/Start。返回菜单使用 Tufty 实体 Home 或 BLE 手柄的 [Home](gamepad-protocol.md#home-返回菜单)。

连接日志包含扫描得到的 MAC 和地址类型。附近有多个 HID 设备时可限定目标，例如：

```text
ble target AA:BB:CC:DD:EE:FF public
save
```

Android 广播地址可能变化，通常保持 `ble target auto`。BLE 不使用串口手柄的 `padid`。串口、BLE 和实体按键可同时使用，按键合并；`swapab` 对合并后的 NES A/B 生效。

需要通过文件固定原手柄 MAC，或设置扫描 UUID 时，将 [examples/gamepad.json](../examples/gamepad.json) 复制到 USB 磁盘的 `config/gamepad.json`。字段及配置优先级见 [存储说明](storage.md#从-usb-磁盘配置手柄)。`0x1812` 是标准 HID 服务 UUID，安卓和实体手柄共用；HID Report ID（日志中的 `report`）用于区分报告类型，不标识哪只手柄。

`ble status` 中按下 Home 时 `home=1`。返回菜单后需松开 Home 和其他按键，才能再次用 A 启动游戏。旧版八键手柄无需修改。

断连时立即释放 BLE 按键并自动重扫。连接失败或连接超时后会自动重启无线控制器，再重新扫描，无需手动 `ble off` / `ble on`；正常等待手柄时不会反复重启。启动或取消连接无响应时也会自动恢复。连接存在时，实体 HID 可只在状态变化时通知，不要求发送串口协议的心跳。空闲的中立报告不会延后菜单自动休眠，持续按键会延后。休眠会关闭无线，按 BLE 手柄不能唤醒 Tufty。

连接就绪后不进行后台 GATT 存活检查，也不因按键静默而主动断开。手机只关闭“手柄”功能、底层蓝牙连接仍在时，Tufty 保持当前连接；重新开启手柄可能需要先关闭再打开手机系统蓝牙，或在 Tufty 执行 `ble scan`。收到实际蓝牙断开事件后，Tufty 立即释放按键并重新扫描。

连接超时的恢复日志为 `CONNECT TIMEOUT` → `CONNECT CANCELLED` → `RECOVER ... restarting controller` → `SCANNING HID`；取消回调缺失时会由超时保护进入恢复。`BLE SCAN` 行的 `reports` 是本轮扫描收到的广播数，`candidates` 是通过 UUID/MAC 筛选并尝试连接的数量。一直没有广播时，可结合手机广播状态排查。

### 验证实体 XTARK 手柄

原手柄接收库提供九字节按键偏移，但未附其实际 Report Map。本固件按该布局适配，实体设备的描述符、订阅和按键仍需实测确认。

在配置文件中填入自己的手柄 MAC 和 public 地址类型，重启并开启手柄。诊断时可在 Console 执行：

```text
ble trace on
ble scan
ble status
```

连接日志按 `CONNECTING` → `READY` 检查。`BLE PEER` 显示实际 MAC、地址类型、HID 服务索引及 Report ID；按键时 `reports` 应增加，`buttons` 应与方向、A/B、Select/Start 对应。还应测试松键、持续长按和关闭再开启手柄。

若出现 `HID FAILED` 或 `INCOMPATIBLE REPORT MAP`，保留前面的 `REPORT MAP` 和状态日志；若能 `READY` 但按键异常，保留 `REPORT` 日志。诊断中的输入 `data` 含 BTstack 补入的 Report ID，正常九字节 GATT 报告在此会显示为十字节。实体 Home 的键位也需检查原始报告确认。完成后执行 `ble trace off` 关闭逐帧输出。

使用 `ble off` 停止扫描和连接；需要时用 `ble on` 恢复，执行 `save` 保存开关和目标设置。USB 磁盘模式不启动无线。

## 管理游戏

通过 Console 的 `usb disk` 或连续双 Reset 进入磁盘模式，把游戏放到盘根目录 `roms/`。完成后使用电脑的安全弹出操作，设备自动返回菜单。磁盘模式下暂停游戏、手柄和 Console 命令。

磁盘界面显示星空和操作提示。开始写入时先显示一帧 `Transferring data!`，随后冻结画面、四颗背灯常亮；最后一次写入完成 500 ms 后切回 `Waiting for data` 并熄灯。读取文件、扫描目录只暂停动画，不触发传输状态或亮灯，也不延长写入指示；读写都停止 500 ms 后恢复动画。电脑后台写入磁盘元数据也会触发写入指示。即使背灯已熄灭，也应先在电脑上安全弹出磁盘。

游戏文件要求：

- 标准 iNES 1.0 或兼容的 NES 2.0 `.nes` 文件，单个文件最多 4 MiB。
- 只扫描 `roms/` 第一层，最多 128 个游戏。
- 文件名最多 255 个 UTF-8 字节，菜单按文件名排序。
- 当前字体只显示 ASCII，建议使用英文文件名及两位数字前缀。

NES 2.0 当前支持标准 NES、NTSC、已启用的 0–255 号 mapper 且 submapper 为 0；PRG/CHR 必须使用普通大小编码，分别不超过 255 × 16 KiB / 255 × 8 KiB。PRG-RAM 和 CHR-RAM（含非易失部分）各最多 8 KiB，不支持同时使用 CHR-ROM 与 CHR-RAM、额外 ROM 数据或特殊外设。扩展控制器字段接受 0（未指定）和 1（标准手柄）。固件在内存中转换头部，不修改原文件，也不增加 mapper 支持；不兼容的扩展字段会明确报错。

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
