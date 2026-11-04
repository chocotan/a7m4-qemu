# 源码来源

## 1. Sony 官方 Ver.6.00 系统源码（全部 40 个包）

- 页面：<https://oss.sony.net/Products/Linux/DI/ILCE-7M4_Ver600.html>
- 页面快照：[`source/ILCE-7M4_Ver600.html`](source/ILCE-7M4_Ver600.html)
- 使用须知快照：[`source/notice.html`](source/notice.html)
- 下载列表：[`source/upstream/urls.txt`](source/upstream/urls.txt)
- 校验清单：[`source/upstream/SOURCE-SHA256SUMS`](source/upstream/SOURCE-SHA256SUMS)
- 本机下载目录：`source/upstream/`（约 400 MiB；源码包被 `.gitignore` 排除，URL 与校验清单保留；按清单自行下载即可）
- 页面快照/须知/清单的摘要：[`source/SOURCE-SHA256SUMS`](source/SOURCE-SHA256SUMS)

全部 40 个包已下载并通过 `sha256sum -c` 校验。`linux-kernel.tar.gz` 的源码版本为
`4.9.337`，与更新包内核 `4.9.337-scel` 同代；`arch/arm64/configs/cxd90057_set_defconfig`
确认了 CXD90057 平台配置。其余 GStreamer、glibc、busybox、e2fsprogs 等包只作为上游源码
留存和事实核对，不参与本运行时的构建。

<details>
<summary>40 个源码包及 SHA-256</summary>

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `elpspacc.tar.gz` | 42950 | `5f67fffbfe39f8192bb3eecb157a6345925c55c4913863c9d3b2fda436aac99d` |
| `gst-plugins-base-1.18.6.tar.xz` | 3180564 | `56a9ff2fe9e6603b9e658cf6897d412a173d2180829fe01e92568549c6bd0f5b` |
| `gstreamer-1.18.6.tar.xz` | 2713892 | `4ec816010dd4d3a93cf470ad0a6f25315f52b204eb1d71dfa70ab8a1c3bd06e6` |
| `GStreamer-gst-plugins-bad-1.18.6.tar.gz` | 8710094 | `e50bc0ac3ba794656bc5643dcdbc5fa1a579a94d1ed4bbae1168bf75e58184cd` |
| `libimobiledevice.tar.gz` | 265393 | `16fea14765180f1e72fd82449931862b7a76dbe1bb297a868ed98963ba651343` |
| `libplist.tar.gz` | 191514 | `d2cdd37d777e052b85a4c476c89f05ffbecbfc5b91ccfdd0b48d457d40e54881` |
| `libusbmuxd.tar.gz` | 45766 | `e7964abacc478174deb2ff752889a151305b4f00e96e7908ea20742039d36552` |
| `linux-kernel.tar.gz` | 109985000 | `e9f708acebf62bd5659cc578101db0d9aa308a4fc413586502451e8a58c80b0e` |
| `qrencode.tar.gz` | 305020 | `73989af54b5ad96e3a5b65c3f122e1bc4741caad32827ac1a610e2f3c8ad70b3` |
| `RTMP-Dump-v2.4.tar.gz` | 92210 | `f2f4c73cd7ff1ce895ae6cee1c6c80aad62b3125f3ad5c21a541b00e1d2978a0` |
| `sony-target-dev-busybox-1.26.2-14000003.src.rpm` | 2145431 | `d71511020e02c363f0f280ae176004ddcc54275515bbbb2e780aa4381365fba9` |
| `sony-target-dev-busybox-1.34.1-14000103.src.rpm` | 2498983 | `25eb74466448f5bec6db9b3c7b8740ac9823a60d2a9510daab2e407536bd426c` |
| `sony-target-dev-coreutils-6.9-14000101.src.rpm` | 5400629 | `1771e5c5f263afa5156b41d0219d1f8d50efc0021d607f75ff84b6794d291d3d` |
| `sony-target-dev-dhcp-4.4.1-14000102.src.rpm` | 11179763 | `147d47abd3114afde38961abca9978a2ef32c69f9d42c6b7cebbe6ce5d2cc470` |
| `sony-target-dev-dhcpcd-8.1.6-14000101.src.rpm` | 236693 | `34e3c782fd0f6166c8023bb94ed86252b1d6b671b90a73c4a18da2eae2133d66` |
| `sony-target-dev-dosfstools-2.11-14000002.src.rpm` | 82284 | `859bb6a4b6a49d83d1b28ae784b48844c1d04e5c4b611a7c91873755a1375ab7` |
| `sony-target-dev-dosfstools-2.11-14000101.src.rpm` | 82925 | `cc4ccac1d8758086e74eaf3bbe958a1ad9131f022ea212c49697bab33c4c2906` |
| `sony-target-dev-e2fsprogs-1.43.4-14000003.src.rpm` | 7555603 | `3f1a3757548957af28332b25127188744029456edae1a5f23431e4cb93118421` |
| `sony-target-dev-e2fsprogs-1.43.4-14000103.src.rpm` | 7557662 | `010c5575d24792bffae48d4238300ceaeb25f2b00a873a00bff8748e338670aa` |
| `sony-target-dev-gcc-gplv3-libs-6.4.0-14000001.src.rpm` | 99901803 | `441f2626af5699d3e68cfb50c34efa385765458e7b38f0bc5328e6ac7f00bc98` |
| `sony-target-dev-gcc-gplv3-libs-6.4.0-14000101.src.rpm` | 99902672 | `a2a17a61ddbb368660f8f528e4231007aecfd4ed54de5169186d4c3967da4e58` |
| `sony-target-dev-glib-2.64.6-14000102.src.rpm` | 10673125 | `913e3572a8f4a6d25a1042a51189ee7e8a3a4564226f932d26898fee5adb5e61` |
| `sony-target-dev-glibc-2.25-14000006.src.rpm` | 13933817 | `54431ed69754d28e2f74cafbc149d4f786ec5ca427c7241e1dd90f1c6efaa526` |
| `sony-target-dev-glibc-2.25-14000106.src.rpm` | 14233801 | `f5b59f8e31bd137db39d148ed28e75b902bd4767e75b2a23955d87f1825cc5b3` |
| `sony-target-dev-hostname-3.18-14000002.src.rpm` | 22600 | `9449576233f99f02144ed153159da44c42b87bd08644d427d8222c99baa23650` |
| `sony-target-dev-hostname-3.18-14000101.src.rpm` | 23144 | `250d416d6bf6d691351925237e453f70f5d277c34e54bcd9f727da830c3587cc` |
| `sony-target-dev-iperf-2.0.5-14000101.src.rpm` | 257223 | `9190f39a16e0b8d898f357049cfe0960622edf4708ffd4079be5f1bcdd56719c` |
| `sony-target-dev-iptables-1.6.1-14000101.src.rpm` | 629544 | `215aa580227620c2666b43b562fe77649849e1dc185670084aa709b46b52ba89` |
| `sony-target-dev-libnl-3.2.25-14000101.src.rpm` | 839331 | `b48c36b5fcc3fbf4754648ea83b1365e1b19b650eeb096769607f3fdb612cfe2` |
| `sony-target-dev-libusb-1.0.21-14000101.src.rpm` | 614930 | `f07f28d29b3d9e976a8ab685afcbe19d703ee9ed3c1d9e517980f296441a770f` |
| `sony-target-dev-libusb-compat-0.1.5-14000101.src.rpm` | 285019 | `fd9fc72e0e04fffcc63910dd2274f2c0a0f08535f5d6829e2c6a2f992ac74d26` |
| `sony-target-dev-pam-1.3.0-14000103.src.rpm` | 2149499 | `d494d2ffbcd7762b96c00c536173a3b70044190c296a6dfdb9525858cdae9462` |
| `sony-target-dev-procps-ng-3.3.12-14000003.src.rpm` | 861385 | `d4c4fbf18e964807c62c90a26a7fb08d8e0f47913f1c8a06f4895d2bfb0ca4ed` |
| `sony-target-dev-procps-ng-3.3.12-14000102.src.rpm` | 863819 | `58f3a75e778570c14399f43c5344548be503858f275843acf114ae0f83a10a12` |
| `sony-target-dev-soundtouch-2.1.2-14000103.src.rpm` | 123172 | `9934a8da0b30fcfdfbed6fdf10845ac54cf81addd81a754dc7332edee4c523fa` |
| `sony-target-dev-util-linux-2.29.2-14000004.src.rpm` | 4292252 | `c4d1c9c904edf626ac6fdeae06cca3263c47e814c69cd8482462ed8c943b8f93` |
| `sony-target-dev-util-linux-2.29.2-14000104.src.rpm` | 4302788 | `941f037a5911ba71ef49e2a4a55f6c671ae6a5bc069541f0d0b7cdd14b9e571e` |
| `SRT.tar.gz` | 1669387 | `1c1f3da2f6c84463ad29377598e53d4e39976ea0b2489c3f8003bf79a6b68000` |
| `usbmuxd.tar.gz` | 66936 | `be1e9922a48ddb9c66f15900362a9c8b43b474863665035b6ab6ab284223f7ea` |
| `zbar-0.10.tar.gz` | 718819 | `343efdf658145ba1d0beab840a9416d5a24032392f3b6e12d3472eacfadfdf22` |
</details>

## 2. 运行用官方更新包（用户自备）

| 项 | 值 |
| --- | --- |
| 文件 | `BODYDATA.DAT`（不在仓库内） |
| 身份 | `model=0x1030081`、`version=6.02`、`region=0`（ALLLANG） |
| 容器 | Sony `BODYDATA.DAT`，FDAT crypter `CXD90057_k8` |
| 包 SHA-256 | `ad6df47a0a93a46336b9920a0d8891d0c766f8c96fa68ebda8685f6c21621188` |
| 说明 | 官方系统源码是 **6.00**；本机可用的更新包是 **6.02**。两者同为 CXD90057/ALLLANG，运行时记录实际包身份，不把 6.02 的二进制冒充 6.00 |

更新包内使用的分区：`nflasha3`（FAT /system）、`nflasha7`（ext2 rootfs）、
`nflasha8`（FAT /cp）、`nflasha15`（ext2 /usr）。`/setting`、`/tmp` 的种子来自
`0800_appli/setting/` 与 `0800_appli/tmp/`。

## 3. QEMU 板级代码、dtc 与 fwtool

| 内容 | 来源 | 说明 |
| --- | --- | --- |
| `source/qemu-sony-native.patch`、`qemu-sony-usb.patch`、`qemu-directory-sd.patch` | 本项目 A7C2/FX30 QEMU 运行时 | CXD90057 板级 UART/GIC 别名、Sony 定时器、输入窗口和目录卡补丁；A7M4 直接复用，未新增 A7M4 专属 QEMU 代码 |
| `source/dtc/`、`source/manifest.json` | 同上，dtc 1.6.x | DTB 编译与运行时 libfdt 改写；构建时做逐文件摘要校验 |
| `runtime/analysis/qemu-virt-board/virt-packed.dtb` | 由 QEMU `-machine virt,dumpdtb=` 生成 | 不是相机 DTB；相机 DTB `cxd90057.dtb` 来自更新包，只作为事实对照 |
| `runtime/analysis/vendor/fwtool-sdk/`、`vendor/fwtool/` | Sony-PMCA-RE（MIT） | DAT/FDAT 解析与 FAT 读取；未改动其逻辑 |
| `runtime/analysis/vendor/host-python/` | PyCryptodomex（Apache-2.0/BSD） | FDAT 解密 |
| `source/vendor/qemu-11.1.2.tar.xz` | QEMU 官方 | 不入库，按 `source/vendor/README.md` 自备；`./build-qemu.sh` 重新构建 |

## 4. 静态分析中使用的对照材料

A7M4 的 `Application_System_Init`、`view::create_screen` 和 view 分发表偏移由本机
ELF 静态分析得到（见[启动报告](report/2026-11-03-a7m4-qemu-bringup/README.md)）。
A7C2/FX30 的同名运行时只用于对照函数布局和已验证的服务行为；没有把 A7C2 固件、
私有设置或运行产物混入 A7M4 资产。

## 5. 不随包分发的内容

Sony 固件、解包后的 `/system`、`/usr`、字体、settings、虚拟卡、完整运行日志和
QEMU 构建目录均不进入公开源码包。第三方许可摘要见 [THIRD-PARTY.md](THIRD-PARTY.md)。
