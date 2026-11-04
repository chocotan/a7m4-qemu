# 第三方组件

| 组件 | 许可 | 用途 | 是否随包分发 |
| --- | --- | --- | --- |
| QEMU 11.1.2 | GPL-2.0 | 模拟器本体；源码按 `source/vendor/README.md` 自备 | 否（`runtime/sources/` 构建产物不入库） |
| dtc / libfdt | GPL-2.0 / BSD-2-Clause | DTB 编译与运行时改写 | 是（`source/dtc/`，保留许可） |
| fwtool（Sony-PMCA-RE） | MIT | DAT/FDAT 解析与 FAT 读取 | 是（`runtime/analysis/vendor/fwtool-sdk/`、`vendor/fwtool/`） |
| PyCryptodomex | Apache-2.0 / BSD-2-Clause | FDAT AES 解密 | 是（`runtime/analysis/vendor/host-python/`） |
| Sony ILCE-7M4 Ver.6.00 开源包 | 各包自带 GPL/LGPL 等 | 官方源码留存与事实核对 | 否（本机 `source/upstream/`，按清单自行下载） |
| Sony ILCE-7M4 更新包 `BODYDATA.DAT` | Sony 专有 | 运行所需原厂系统 | 否 |
| A7C2/FX30 QEMU 板级补丁 | GPL-2.0（本项目） | CXD90057 板级 UART/GIC/定时器与输入/目录卡 | 是（`source/*.patch`） |

本包不包含 Sony 固件、字体、私有设置或虚拟磁盘镜像。Sony 开源包的完整清单、
SHA-256 和官方页面快照见 [SOURCES.md](SOURCES.md)。
