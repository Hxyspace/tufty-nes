# 存储与固件升级

## Flash 布局

固件兼容 `tufty2350-native` 使用的 16 MiB Flash 布局：

| 区域 | 起始偏移 | 容量 | 内容 |
|---|---:|---:|---|
| 固件 | `0x000000` | 2 MiB | 程序 |
| `/rom` | `0x200000` | 1 MiB | RM1 只读资源 |
| `/system` | `0x300000` | 12 MiB | FAT 卷、游戏和素材 |
| `/state` | `0xf00000` | 1 MiB | LittleFS、应用配置 |

刷入前，设备须已具有上述布局及有效文件系统。UF2 不包含文件系统镜像；固件不会自动分区或格式化。当前不提供空白设备的初始化工具。

## 游戏与 USB 磁盘

游戏位于 `/system/roms/`。运行游戏时 FAT 只读；进入 USB 磁盘模式后固件卸载 FAT，由电脑独占读写。只有 `/system` 对外暴露，`/rom` 和 `/state` 不显示在该磁盘中。

FatFs 支持 512、1024、2048、4096 字节逻辑扇区，底层 Flash 按 4096 字节物理块擦写。磁盘操作完成后应安全弹出，再等待设备自动重启。

## 配置

`/state/nes.json` 保存以下字段：

| 字段 | 默认值 | 用途 |
|---|---|---|
| `controller_id` | `1` | 串口手柄 ID |
| `brightness` | `80` | 屏幕亮度，1–100 |
| `swap_ab` | `false` | A/B 互换 |
| `last_rom` | 空 | 上次选择的游戏文件名 |
| `menu_sleep_seconds` | `300` | 菜单空闲休眠秒数，0 关闭，最大 86400 |
| `ble_enabled` | `true` | 启用 BLE HID 扫描和连接 |
| `ble_peer` | 空 | 目标 MAC，空值自动扫描 |
| `ble_random` | `false` | 指定目标使用 random 地址类型；否则 public |
| `ble_scan_uuid` | `"0x1812"` | 自动扫描时匹配的 16 位服务 UUID |

配置通过临时文件和原子重命名写入，其他 State 文件保持原样。State 无效或 NES 配置损坏时使用默认配置，并禁用保存。此文件不包含游戏进度或电池 SRAM。

已有配置缺少新增字段时使用默认值，无需迁移文件。BLE 配对信息仅保存在 RAM，配置文件只保存连接偏好。菜单自动休眠及串口休眠会先保存待写配置；长按 Reset 会先重启，使用之前应保存设置。

### 从 USB 磁盘配置手柄

将 [examples/gamepad.json](../examples/gamepad.json) 复制到设备 USB 磁盘的 `config/gamepad.json`，安全弹出后设备重启加载。固件中的路径为 `/system/config/gamepad.json`。

```json
{
  "ble_enabled": true,
  "ble_peer": "",
  "ble_random": false,
  "ble_scan_uuid": "0x1812"
}
```

- 自动连接安卓或其他兼容 HID：`ble_peer` 留空，`ble_scan_uuid` 保持 `"0x1812"`。这不是安卓专属 ID，附近多个 HID 可能竞争连接。
- 固定原手柄：将 `ble_peer` 改为手柄背面 MAC，例如 `"03:25:20:32:28:98"`；请替换为自己的地址。参考 XTARK 手柄使用 public 地址，`ble_random` 保持 `false`。指定 MAC 时按地址及地址类型筛选，不要求广播列出扫描 UUID。
- `ble_scan_uuid` 必须是字符串 `"0x"` 加四位十六进制数。它只控制广播筛选；连接后仍查找标准 HID Service `0x1812`，修改它不会增加其他 GATT 协议支持。
- 可选 `controller_id`（1–65535）设置 CDC1 串口的 TGP ID；与 BLE 的服务 UUID、MAC、Report ID 分别独立。

文件最多 1023 字节，使用合法 JSON。开机先加载 `/state/nes.json`，再用此文件中提供的字段覆盖；未提供的字段沿用 State 配置。文件不存在则使用 State 配置；读取失败或内容无效时整份文件忽略，不部分应用字段，并输出 Console 日志。

Console 命令可修改本次运行的设置，`save` 仍写入 `/state/nes.json`，不会回写 FAT。下次开机时 `config/gamepad.json` 中的字段仍有优先级；需要长期修改时编辑该文件，或删除对应字段后通过 Console 保存。

## 升级注意事项

普通固件数据限制在前 2 MiB。UF2 同时保留 Pico SDK 默认的 RP2350-E10 兼容块，其行为取决于芯片修订版本：

- **A3 及以后**：Boot ROM 忽略带标签的兼容块。
- **A2**：Boot ROM 可能写入该块，擦除 State 最后一个扇区；升级前应备份配置。

A2/A3 指芯片修订版本，与 RP2350A/B 封装不同。兼容块地址为 `0x10ffff00`，family 为 `0xe48bff57`，行为与 SDK 默认设置一致。构建后的 UF2 检查分别验证普通固件范围和兼容块。
