# Tufty NES 手柄协议

普通模式使用 CDC1（Gamepad），115200 波特率，打开时启用 DTR；CDC0 用于 Console。协议支持 TGP v1 五键及 TGP v2 NES 八键状态。磁盘模式不处理手柄消息。

## 会话

每条消息以 ASCII LF 结尾，也接受 CRLF。设备在连接时发送 `#TGP:APP\n`。

```text
PC -> Tufty: #TGP:HELLO:0001:0123456789abcdef
Tufty -> PC: #TGP:READY:0123456789abcdef
PC -> Tufty: #TGP:5447500201000081
Tufty -> PC: #TGP:ALIVE:0123456789abcdef
PC -> Tufty: #TGP:END:0123456789abcdef
Tufty -> PC: #TGP:BYE:0123456789abcdef
```

`HELLO` 中的 ID 是 4 位十六进制（1–65535，默认 0001），必须与设备 `padid` 一致。nonce 是 16 位小写十六进制，每次新会话生成；READY、ALIVE、END、BYE 必须匹配。切换会话、断开 DTR、END 均释放全部按键。

每 120 ms 至少发送一个状态帧，按键变化时立即发送。设备每 500 ms 发 ALIVE；350 ms 无有效状态帧则松开按键，1200 ms 无有效状态帧则结束会话。PC 也在 1200 ms 无 READY/ALIVE 时重新握手。状态帧通过当前会话接受，帧本身沿用 v1 格式，不携带 nonce。

## 状态帧

二进制结构固定 8 字节，转成 16 位十六进制字符串并包装为 `#TGP:<hex>\n`；不直接发送二进制。

| 字节 | 内容 |
|---|---|
| 0–2 | ASCII `TGP` |
| 3 | 版本 1 或 2 |
| 4–5 | controller ID，小端 |
| 6 | sequence，每次状态帧加 1，模 256 |
| 7 | buttons，1 表示按下 |

同一会话连续相同 sequence 的帧被拒绝；按键无变化的心跳也必须增加 sequence。不支持的版本、错误 ID、错误长度、v1 未定义按键位会被拒绝。

| bit | TGP v1 | TGP v2 |
|---|---|---|
| 0 | Up | A |
| 1 | Down | B |
| 2 | Left | Select |
| 3 | Right | Start |
| 4 | ACTION → NES A | Up |
| 5 | 禁止 | Down |
| 6 | 禁止 | Left |
| 7 | 禁止 | Right |

例如 `#TGP:5447500201000081\n`：v2、ID 1、sequence 0、A + Right。

## 返回菜单

已连接会话可以发送 `#TGP:MENU:<nonce>\n`，释放远程按键并请求退出游戏。PC 发送器把 Esc 映射到此命令，Tufty Home 也返回菜单。回到菜单后等待按键松开，避免立即重新启动游戏。

## 兼容性

TGP v1 的五键状态、握手、超时与 END 会话仍保留；原发送器的 USB 自动发现规则需要匹配本项目 `CAFE:4025` 和 CDC1。提供的 `tools/serial_gamepad.py --v1` 可用于验证。v1 只能控制方向与 NES A，完整 NES 操作应使用默认 v2。

## BLE HID

BLE 使用 HID over GATT，Tufty 为 Central。扫描 HID Service `0x1812`，以扫描结果的 MAC 和地址类型连接，读取 Report Map `0x2A4B`、Report Reference `0x2908`，订阅 Input Report `0x2A4D` 的 CCCD。使用 Report Protocol，不发送 Suspend 控制命令。

支持 Android NES 八键 / XTARK HID 的九字节输入布局，GATT 通知不附加 Report ID 前缀：

| 字节 | 内容 |
|---|---|
| 0–3 | 两组摇杆 X/Y，NES 忽略 |
| 4 | Hat：0 上、1 右上、2 右、3 右下、4 下、5 左下、6 左、7 左上、255 中立 |
| 5 | bit 0 = A，bit 1 = B |
| 6 | bit 2 = Select，bit 3 = Start，bit 4 = Home |
| 7–8 | 扳机模拟值，NES 忽略 |

例如 `80 80 80 80 02 01 00 00 00` 表示右 + A，`80 80 80 80 FF 00 00 00 00` 表示全松开。报告长度必须恰好 9 字节，无效报告清空 BLE 按键。接收前检查描述符中的报告大小和 Hat 偏移；不把键盘或其他布局直接按固定偏移解释。

BTstack 在其内部 HID 事件中补回 Report ID，应用按发现的 Report ID 和服务索引筛选后去掉该前缀。BLE 无 controller ID、sequence 或 TGP 会话；断连时清空输入并重新扫描。支持只在变化时通知的实体 HID，不对有效长按套用串口的 350 ms 超时。Select/Start 保持 NES 按键语义，不触发系统菜单。

连接就绪后不周期性查询 HID 服务、CCCD 或 Input Report，不要求发送端心跳，也不根据通知静默判断断联。仅关闭手机的“手柄”功能而底层链路仍在时，Tufty 保持连接；收到实际蓝牙断开事件后释放按键并重扫。必要时关闭再打开手机系统蓝牙，或用 `ble scan` 主动重新连接。

### Home 返回菜单

Home 使用 byte 6 的 bit 4（掩码 `0x10`），按下返回游戏菜单，松开后才能重新启动游戏。在菜单中按住 Home 会阻止启动游戏，并延后自动休眠。沿用 XTARK 九字节布局；安卓 Report ID 保持 1，GATT 通知不加 ID 前缀。

固件按固定功能键位解码 Home，无额外描述符协商。没有 Home 的八键发送端保持该位为 0 即可。

安卓复刻同一报告布局，在原有 Select/Start 的基础上加入 Home。Report Map 使用普通 Button Usage Page `0x09`，Select/Start/Home 分别为 Button 9/10/13，位于 byte 6 bit 2/3/4；相关段落为：

```text
95 03 09 09 09 0A 09 0D 81 02  # Select/Start/Home；继承 Usage Page=9、Report Size=1、逻辑范围 0..1
95 03 81 03                      # byte 6 bit 5..7 填充
75 08 95 02 81 03                # 原有 byte 7/8
C0
```

发送端生成 byte 6 时，在原有 Select/Start 值上合入 Home：`(select ? 0x04 : 0) | (start ? 0x08 : 0) | (home ? 0x10 : 0)`。按下与松开时发送完整状态，不要求心跳。

```text
80 80 80 80 FF 00 10 00 00  # 仅 Home 按下
80 80 80 80 FF 00 00 00 00  # 全松开
```

`ble status` 中的 `home` 表示当前是否按住，`buttons` 仍只表示 NES 八键。

参考 XTARK 接收库将 byte 6 整字节作为功能键，定义了 L2/R2、Select/Start、L3/R3，但未命名 `0x10`，也未提供实体手柄的 Report Map。因此 byte 6 bit 4 是本项目与安卓发送端约定的 Home 位；实体手柄的 Home 是否使用此位，需通过其原始通知确认。
