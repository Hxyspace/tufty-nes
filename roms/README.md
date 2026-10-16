# 本地游戏目录

此目录用于存放本地准备的标准 iNES 1.0 `.nes` 文件和封面。游戏及封面不纳入版本管理，也不打包进固件。

菜单按文件名排序，建议命名为 `01_Game.nes`、`02_Game.nes`。封面使用相同文件名主干，如 `01_Game.rgb565`。

在仓库根目录执行：

```bash
python3 tools/deps.py --profile host
python3 tools/prepare_roms.py
python3 tools/prepare_roms.py --fix --trim-padding
```

默认只检查；`--fix` 会先备份到 `build/rom-backups/`。进入设备的 USB 磁盘模式，把处理后的 `.nes` 和可选封面复制到盘根目录 `roms/`，再安全弹出。

游戏格式限制、修复选项和封面规格见 [使用指南](../docs/usage.md)。
