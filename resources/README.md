# 固件资源

构建时将以下三个目录分别打包到对应分区，保留目录中的相对路径：

| 源目录 | 设备位置 | 格式 / 容量 |
|---|---|---|
| `resources/rom/` | `/rom/` | RM1 / 1 MiB |
| `resources/system/` | `/system/` | FAT12，4096 字节扇区 / 12 MiB |
| `resources/state/` | LittleFS 根 `/` | LittleFS v2 / 1 MiB |

默认只读资源包含 `nes-background.rgb565`：320×200、无文件头的小端 RGB565，与程序内置菜单背景一致。可以替换同名文件，再刷入带 ROMFS 的固件更新背景。

游戏和同名 `.rgb565` 封面放在 `resources/system/roms/`，完整文件系统镜像会将其部署到设备。默认只创建空 `roms/` 目录，不附带游戏，也不会自动打包仓库根目录 `roms/` 中的文件。

配置源路径为 `resources/state/state/nes.json`，对应设备 `/state/nes.json`；第二层 `state` 是卷内目录，不能省略。默认保留空目录，由程序使用默认配置，保存时创建文件。

游戏目录和配置目录中的本地文件默认被 Git 忽略，但会参与构建；发布完整镜像前应检查其内容。`.gitkeep` 用于保留空目录，所有隐藏文件和隐藏目录均不进入镜像。禁止符号链接，FAT 文件名大小写冲突及容量不足会导致构建失败，不会截断资源。

运行 `./tools/build.sh` 同时生成三种固件，见[构建指南](../docs/building.md)。日常升级用 `tufty-nes-flash.uf2`；更新只读素材用 `tufty-nes-with-romfs.uf2`；初始化设备用 `tufty-nes-with-filesystem.uf2`，它会**替换已有资源、游戏和配置**，不与设备上的文件合并。

带资源的 UF2 省略文件系统未使用的擦除扇区，分区容量保持不变。完整镜像会重建目录及配置，但未使用扇区内的旧数据不保证被擦除；此操作不用于安全擦除。
