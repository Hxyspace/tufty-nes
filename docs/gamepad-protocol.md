# Tufty NES 串口手柄协议

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

TGP v1 的五键状态、握手、超时与 END 会话仍保留；原发送器的 USB 自动发现规则需要匹配本项目 `CAFE:4025` 和 CDC1。提供的 `tools/serial_gamepad.py --v1` 可用于验证。v1 只能控制方向与 NES A，完整 NES 操作应使用默认 v2。BLE 不在本版范围内。
