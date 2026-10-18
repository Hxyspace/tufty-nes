# Tufty NES 手柄协议

Tufty 自身按键、CDC1 串口手柄和 BLE 手柄可同时使用，输入合并；`swapab` 对合并后的 NES A/B 生效。USB 磁盘模式不处理手柄输入。连接及日常操作见 [使用指南](usage.md#ble-手柄)。

## USB 串口

CDC0（Console）用于设置，CDC1（Gamepad）用于手柄。CDC1 使用 115200 波特率，打开时启用 DTR，支持 TGP v1 五键及 TGP v2 NES 八键状态。

### 会话

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

### 状态帧

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

### 返回菜单

已连接会话可以发送 `#TGP:MENU:<nonce>\n`，释放远程按键并请求退出游戏。PC 发送器把 Esc 映射到此命令，Tufty Home 也返回菜单。回到菜单后等待按键松开，避免立即重新启动游戏。

### 兼容性

TGP v1 的五键状态、握手、超时与 END 会话仍保留；原发送器的 USB 自动发现规则需要匹配本项目 `CAFE:4025` 和 CDC1。提供的 `tools/serial_gamepad.py --v1` 可用于验证。v1 只能控制方向与 NES A，完整 NES 操作应使用默认 v2。

## BLE HID

Tufty 作为 Central，默认扫描 HID 服务 `0x1812` 并自动连接。当前支持实体 XTARK 手柄及采用相同布局的安卓发送端；不使用串口的 ID、序列号和会话握手。指定 MAC 或恢复自动连接的方法见 [使用指南](usage.md#连接设置)。

### 报告格式

读取 Report Map（`0x2A4B`），通过 Report Reference（`0x2908`）区分报告，并订阅 Input Report（`0x2A4D`）的 CCCD。游戏使用 **Report ID 4、10 字节 payload**，GATT 通知不带 ID 前缀；Consumer Report ID 3 不映射为 NES 输入。固件检查描述符布局后才按以下偏移读取。

| 字节 | 内容 |
|---|---|
| 0–1 | 左摇杆 X/Y，0 左/上、128 居中、255 右/下 |
| 2–3 | 右摇杆 X/Y，坐标同上，暂不控制 NES |
| 4 | 低四位方向：0 上、1 右上、2 右、3 右下、4 下、5 左下、6 左、7 左上；8–15 松开，高四位为填充 |
| 5 | 数字按钮：A=`01`、B=`02`、X=`08`、Y=`10`、LB=`40`、RB=`80` |
| 6 | 数字按钮：LT=`01`、RT=`02`、Select=`04`、Start=`08`、安卓 Home=`10`、L3=`20`、R3=`40` |
| 7–8 | RT / LT 按压程度，0 松开、255 完全按下 |
| 9 | 填充，普通状态发送 `00` |

掩码为十六进制，组合键按位 OR。初始和全部松开时发送：

```text
80 80 80 80 FF 00 00 00 00 00
```

Read 返回当前完整状态，按键、摇杆或扳机变化时通知。无需心跳；长按和静置不会超时释放或触发重连。断连时立即释放 BLE 输入并重新扫描。实体手柄使用 LE Legacy Just Works 配对，配对信息仅存 RAM。

### 游戏操作与返回菜单

NES 使用方向、A/B、Select/Start。左摇杆带回中死区，可代替十字键；十字键有输入时优先。X/Y、肩键和扳机不映射额外 NES 动作。

实体手柄的 Home 当前不发送输入，可将左右摇杆回中、松开其他按键和扳机，同时按下 **L3+R3 并保持 1.5 秒**返回菜单。触发后需松开两个摇杆按键才能继续操作。安卓也可使用 byte 6 的 `10` 位直接发送 Home，松开时清除此位。

### 安卓关闭手柄

通过已订阅的 ID 4 游戏报告发送以下固定 10 字节消息，不带 ID 前缀：

```text
54 47 50 01 42 59 45 00 00 A5
```

这是主动关闭消息，必须完整匹配；普通状态不能仅靠修改填充字节请求断开。Tufty 收到后释放输入、主动断开当前手柄并继续扫描，不修改 BLE 开关或保存的设置。

安卓先停止普通输入通知和广播，发送关闭消息后保留 GATT Server，等待断开回调再清理，可设置约 2 秒的清理超时。重新开启时恢复中立状态及广播，无需关闭手机系统蓝牙。完整 Report Map 及安卓发送端实现细节见安卓手柄项目的 `docs/hid-protocol.md`。
