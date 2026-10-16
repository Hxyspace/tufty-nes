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
python3 tools/deps.py                 # 准备固件依赖
python3 tools/deps.py --profile host  # 只准备主机测试与 ROM 检查依赖
python3 tools/deps.py --check         # 检查已下载版本和补丁
python3 tools/deps.py --radio         # 额外准备 CYW43、BTstack 源码
```

上游只应用两个补丁：InfoNES 周期服务钩子和 FatFs 外部配置入口。板级定义、存储配置和应用构建均由主项目维护。脚本会识别已应用的补丁；版本不符、补丁冲突或依赖中存在额外修改时会停止，保留本地文件。

升级依赖时，应更新锁文件、核对补丁并运行测试。对依赖的适配修改放入 `patch/`，应用功能放入 `src/`。`--radio` 仅下载源码，当前固件尚未接入 BLE。

## 测试

```bash
sudo apt install dosfstools mtools
./tools/test.sh
```

主机测试覆盖手柄协议、Console、菜单休眠计时与休眠请求、USB 磁盘界面与传输状态、512/4096 字节 FAT、配置保存与错误处理、ROM 修复、UF2 检查，以及依赖补丁与本地修改保护。C/C++ 测试启用 ASan/UBSan，使用临时文件系统镜像。实际低功耗状态和物理唤醒需在设备上验证。

## 项目结构

```text
src/        应用、显示、菜单、存储与输入
board/      Tufty 板级定义
config/     FatFs、TinyUSB 配置
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
