# 构建指南

## 环境

固件构建目前在 Linux 上验证，需要：

- Git、Python 3、CMake 3.21+、Ninja。
- 宿主 C/C++ 编译器。
- ARM GNU Toolchain，含 newlib 和 libstdc++。

```bash
sudo apt install git python3 cmake ninja-build build-essential \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
./tools/build.sh
```

首次构建需要访问 GitHub 下载依赖，以后可离线复用。脚本会构建宿主 picotool，再编译固件并检查 UF2 写入范围。

## 输出

| 文件 | 用途 |
|---|---|
| `dist/tufty-nes-flash.uf2` | BOOTSEL 刷机文件 |
| `dist/tufty-nes-flash.elf` | 调试符号及 ELF 固件 |
| `dist/build-info.json` | 源码提交、依赖版本、补丁及产物校验值 |

发布目录同时包含用户文档和许可证。构建不需要游戏 ROM。

可配置的环境变量：

| 变量 | 默认值 |
|---|---|
| `PYTHON` | PATH 中的 `python3` |
| `CMAKE_BUILD_PARALLEL_LEVEL` | `4` |
| `TUFTY_BUILD_DIR` | `build/firmware` |
| `TUFTY_ARTIFACT_DIR` | `dist` |
| `TUFTY_TEST_BUILD_DIR` | `build/tests` |

## 烧录

将设备置于 BOOTSEL 模式，将 UF2 复制到 RP2350 磁盘即可刷入。刷入前请阅读 [存储与升级说明](storage.md)。

若系统已安装支持 USB 的 picotool，也可执行：

```bash
./flash_tufty.sh --flash
```

该命令会重新构建、烧录、校验并重启设备。不带参数的 `./flash_tufty.sh` 只编译。项目自行构建的 picotool 不含 libusb，仅用于生成 UF2。

## 依赖与补丁

依赖 URL 和完整 commit SHA 保存在 [deps.lock.json](../deps.lock.json)。依赖下载到 Git 忽略的 `lib/`，无需初始化主仓库 submodule。SDK 内部依赖按其 gitlink 固定版本下载。

```bash
python3 tools/deps.py --radio         # 准备完整固件依赖，含 CYW43、BTstack
python3 tools/deps.py --profile host  # 准备基础协议、存储与 ROM 检查依赖
python3 tools/deps.py --radio --check # 检查固件依赖版本和补丁
python3 tools/deps.py --profile host --radio # 准备含真实 HID 解析器的主机测试
```

上游补丁包括 InfoNES 周期服务钩子、FatFs 外部配置入口、PPU 完整可见行与左侧遮罩修正，以及 mapper 48 的 1 KiB CHR 银行寻址修正。板级定义、存储配置和应用构建均由主项目维护。脚本会识别已应用的补丁；版本不符、补丁冲突或依赖中存在额外修改时会停止，保留本地文件。

升级依赖时，应更新锁文件、核对补丁并运行测试。对依赖的适配修改放入 `patch/`，应用功能放入 `src/`。

## 双核与输入服务

Core 0 运行模拟器、菜单、TinyUSB 和 BLE；Core 1 负责屏幕转换及 PIO/DMA 输出。串口输入量较小，BLE 使用 CYW43 polling 架构，在模拟器周期钩子及菜单/帧等待中服务，不占用显示核。服务间隔目标为 1 ms，无线 HCI 每次最多处理 4 轮。`ble status` 的 `poll_gap_ms` 记录最大实际轮询间隔，`poll_work_ms` 记录 CYW43 单次轮询最大耗时，`restarts` 记录超时恢复次数；前者还包含两次轮询间其他工作耗时及首次控制器初始化。

输入与设备管理按以下职责分层：

| 模块 | 职责 |
|---|---|
| `storage.*` | 加载、校验和保存 `/state/nes.json` |
| `controllers.*` | Console 与后续设置菜单共用的来源开关、白名单编辑接口；将配置快照传入运行层 |
| `bluetooth_manager.*` | 设备候选缓存、限时发现、临时选择、白名单筛选与首选设备策略，不依赖 HID、BTstack 或文件系统 |
| `ble.*` | CYW43/BTstack 驱动、广播解析、物理扫描/连接/配对、断开和故障恢复 |
| `ble_gamepad.*` | HID 服务与 Report Map 检查、报告选择和接收，不管理扫描列表、白名单或配置文件 |
| `ble_tgp.*` / `tgp_legacy.hpp` | 自定义 BLE TGP 服务订阅，以及串口／BLE 共用的 v1/v2 逻辑状态解码 |
| `usb.*` / `protocol.*` | CDC1 接收及 TGP v1/v2/v3 分帧、握手、序列检查与超时 |
| `gamepad.*` | 共用 Report 解码、完整 `ControllerState`、每来源摇杆滞回和 Home 手势、NES 按键派生 |
| `input.*` | 单一活跃远程来源仲裁、本机合并、NES 输出及菜单 `held/pressed/released` 事件 |

串口 v3 与 BLE HID 通过共用解码器生成完整控制器状态，保留双摇杆、压力和全部按键；TGP v1/v2 适配器直接提交 `ControllerState`，未提供的摇杆／压力保持中立。两个 BLE profile 共用一个输入接收器，同一时刻只建立一条连接。菜单边沿按到达累计，读取时消费；GPIO 直接提交本机状态，无需包装成 Report。应用从统一输入层读取，不单独合并各传输层的接收器。

BLE 发现和连接请求由服务循环执行，扫描列表使用固定容量快照。存储层只在加载和保存时读写配置，运行中的连接层使用设置服务传入的配置快照，HID 适配层无需读取配置。白名单使用 MAC 与地址类型，不能将安卓轮换的随机地址当成永久身份。

连接超时先取消连接，再关闭 HCI 控制器；连接失败、启动或取消/断开无响应时也会关闭控制器，等待 OFF 后自动重新启动。正常空闲扫描及已就绪连接不会因输入静默触发此恢复；就绪后不发起周期性 GATT 检查。运行时恢复保留栈及事件注册，不重复初始化 GATT，也不写入 Flash 配对库。调用可能同步回调的 BTstack API 前先设置等待状态，避免回调的完成状态被覆盖。

USB 磁盘模式不启动无线。深度休眠前释放按键并关闭无线栈。BLE 临时配对记录只存 RAM，启动包装器和 RAM 数据库避免 SDK 默认 Flash TLV 占用 `/state`；重启后重新连接。

无线采用单核主动轮询，驱动中的短延时使用硬件计时忙等，不依赖闹钟或信号量唤醒；菜单等候显示核时持续服务 USB/BLE。该适配参考 native 的等待处理，另覆盖 BLE 总线唤醒时的毫秒延时。忙等期间不递归调用无线或 USB 服务；不改变菜单深度休眠策略。

## 测试

```bash
sudo apt install dosfstools mtools
./tools/test.sh
```

主机测试覆盖串口协议、BLE 状态机和真实 BTstack HID 描述解析、Console、菜单休眠计时与休眠请求、USB 磁盘界面与传输状态、512/4096 字节 FAT、配置保存与错误处理、ROM 解析与修复、PPU 可见行与遮罩、mapper 48 图块寻址、UF2 检查，以及依赖补丁与本地修改保护。C/C++ 测试启用 ASan/UBSan，使用临时文件系统镜像和合成图块。无线连接、输入延迟、实际游戏画面、低功耗状态和物理唤醒需在设备上验证。

## 项目结构

```text
src/        应用、显示、菜单、存储与输入
board/      Tufty 板级定义
config/     FatFs、TinyUSB、BTstack 配置
cmake/      构建辅助与固件范围约束
patch/      上游适配补丁
tools/      依赖、构建、ROM 和 PC 手柄工具
tests/      自动回归测试
docs/       使用与开发文档
licenses/   素材和参考实现的许可证
lib/        下载的依赖，不提交
roms/       本地游戏，不提交
```

内置菜单素材已生成并纳入源码。重生成需要 Pillow、Lato 字体及 `TUFTY_yuan` 素材目录：

```bash
python3 tools/generate_menu_assets.py --reference /path/to/TUFTY_yuan
```

磁盘界面的 ASCII 像素字体来自 Pimoroni `tufty2350` 的 Ark/Absolute 字体，已生成并纳入源码。重生成：

```bash
python3 tools/generate_disk_fonts.py --reference /path/to/tufty2350
```
