# A7M4（ILCE-7M4 6.02）QEMU 移植：启动链与 UI 阻塞

日期：2026-11-03。范围：本机隔离 QEMU，不接真机、不写真实 SD、不刷固件。
本轮把 A7M4 官方更新包导入为可启动 guest，加载原厂内核、模块和
`bootin.elf → im.elf`，并定位到原厂 UI 未能建屏的具体前沿。本文把**已实际消费的证据**、**否定的尝试**和**仍缺的硬件/协议**分开记录。

**2026-11-04 更新**：实验路径的原boot已自然越过`0x315`并进入语言选择屏`0x12e`。
原LZ资源解压及72次GPE COPY产生完整语言页，QMP原bitmap读回355155B非零；
实际AP帧consumer保存并读回像素后回复完成，已观察到帧序号256。后续新增`--skip-setup`，经原设置和日期API已直接进入拍摄主界面，见[跳过首设成果](2026-11-04-skip-first-setup.md)。交互按键、CMOS与成片保持未验证。最新验证与旧判断更正见
[原 UI 初始化链](2026-11-04-original-ui-chain.md)。下节保留 11-03 默认基线的结论。

## 1. 11-03 基线结论

- **已完成**：官方 A7M4 6.02 ALLLANG 包识别、分区解包、rootfs/usr/system/cp
  组装、固定地址内核启动、原厂 initrc 模块顺序、`bootin.elf` 与 `im.elf`
  启动、stella/UI 引擎堆日志；A7C2 observer 预载实验中还观察到
  `Application_System_Init` 返回与 HDMI 0x127 请求（该实验日志未保留，不作为默认证据）。
- **未完成**：原厂 `view::create_screen` 没有被调用；没有拍摄页面、菜单、
  取景、CMOS、镜头/AF、编码、拍照/录像或虚拟 SD 成片。`capture_verified=false`。
- **不能声称**：这不是“A7M4 完整硬件仿真”，也不是“原厂 UI 已恢复”。
  当前是一份可复现的启动/应用层移植基线。

## 2. 官方源码下载

已从 Sony 官方页面下载全部 **40 个** Ver.6.00 源码包，页面与须知快照、URL
列表、逐文件 SHA-256 均在 `source/`。`sha256sum -c` 全部通过；文件总大小约
400 MiB。`linux-kernel.tar.gz` 为 4.9.337，与运行包内核 4.9.337-scel 同代。
详细清单与来源见 [SOURCES.md](../../SOURCES.md)。

运行使用本机已有的 **6.02** 更新包（`BODYDATA.DAT`，SHA-256
`ad6df47a…`），因为 6.00 源码页不提供更新包本体。运行时 manifest 记录实际
包身份，不把 6.02 二进制写成 6.00。

## 3. 启动装配

| 项 | 值 | 来源/核对 |
| --- | --- | --- |
| DRAM 基址 | `0x400000000` | `/proc/iomem`、`cxd90057-memmap.dtsi`、A7C2/FX30 运行时 |
| DTB 装载 | `0x400002000` | 沿用已验证的 CXD90057 板级布局 |
| 内核装载 | `0x400080000` | 同上 |
| initrd / cpio | `0x440000000` | 本运行时把 rootfs 打成 raw newc cpio；A7M4 内核无 gzip initrd 支持 |
| 内存 | QEMU `-m 24576`，DTB 声明 `0x400000000 + 0x140000000` | 覆盖 A7M4 `memrsv=` 保留区并给 ramdisk 留空间 |
| 内核 cmdline | 只转发 `memrsv=`，不转发 `mem=4G`、`cfg.uart=1` 等 | 后者在 A7C2/FX30 已实测会破坏启动 |
| 启动期黑名单 | `__udif_init_udif_wdt/fusb_otg/pcie/ddrc/pcidma/sony_usb_init` | 缺硬件时不挂死；`udif_hwtimer` 保留，由 QEMU `sony-hwtimer` 提供 |

`a7m4_qemu.py` 在 QEMU virt DTB 上写入 `bootargs`、initrd 区间、内存节点、
Sony iomap 别名和 `memrsv` 保留区；A7M4 的 `kemco.txt` 与 A7C2/FX30 的
`memrsv` 表不同，按包内文件逐项转发。

## 4. 根文件系统

更新包内四个分区：

| 分区 | 类型 | 用途 | 本运行时 |
| --- | --- | --- | --- |
| `nflasha3` | FAT12 | `/system` | 解到 `menu-root/system/` |
| `nflasha7` | ext2 | `/rootfs` | 解到根目录 |
| `nflasha8` | FAT12 | `/cp` | 解到 `menu-root/cp/` |
| `nflasha15` | ext2 | `/usr` | 合并到 `menu-root/usr/` |

`/setting` 与 `/tmp` 的种子来自 `0800_appli/setting/` 和 `0800_appli/tmp/`；
`Backup.bin` 使用 `0110_backup/SYSIPSX-DSLR/LS/CH89101_ALLLANG.bin`，`lensfile.bin`
来自 `0800_appli/lens/`。`/etc` 采用更新包的 `tmp` 种子（toprc、passwd、
nsswitch、pam.d、字体等），因为设备上 `/etc` 与 `/var` 同挂 nflasha10，而该
分区不在更新包内。

从镜像中移除的只有会在本板空指针崩溃的 USB 内核模块：`usb_current`、
`usb_buspwr`、`usb_beets`、`usb_portMonitor`、`usbg_buspower`、`usbg_sen`、
`usb_extcmd`。同时移除会尝试访问 USB IC 的 `libUsbBusPower.so`；
`libInfraUsbCmn.so` 与 `libUsbControl.so` 保留，因为 `libObj.so` 直接依赖它们。
脚本的 `removed_libs`/`removed_modules` 只记录实际删除的文件。

## 5. 实际运行证据

最终默认运行：`runtime/analysis/menu-runs/app-20261103-184605/`（本机私有，
不入库）。脱敏副本：

- [完整控制台 gzip](../../validation/evidence/2026-11-03-app-console.log.gz)
- [关键行摘录](../../validation/evidence/2026-11-03-app-key-lines.txt)
- [机器可读摘要](../../validation/evidence/2026-11-03-app-evidence.json)
- [同次 QMP 截图（无 UI）](../../validation/evidence/2026-11-03-no-display.png)

关键内容：

```
Linux version 4.9.337-scel ... #1 SMP PREEMPT Sat Apr 18 02:42:38 JST 2026
A7M4_GUEST_READY
A7M4_SESSION_START
MOD_OK devno=0
...
MOD_OK dma330=0
...
MOD_OK dmm=0
...
MOD_OK codec_drv=0
A7M4_SESSION_MODULES_DONE
A7M4_SESSION_APP_STARTED
 FixedAreaTop  : 10268000
 FixedAreaSize : 0007C000
 Physical Addr : 102BB400
 liro: Unknown symbol pciatu_unmap (err 0)
 liro: Unknown symbol pciatu_map (err 0)
CGL-VERSION: "20180404MCA"
I/stella(   254): Created HeapAllocator: 0x7f84000920((nil))
    mpMemoryHeap on 0x0000000410524000-0x0000000410AE6040 (6037568) ...
```

这些行来自原厂内核/进程本身：内核版本、原厂 initrc 模块、原厂 `bootin.elf`/
`im.elf` 的 FixedArea、原厂 liro 模块的符号错误、原厂 stella 和 DMM 堆日志。
没有主机重绘、没有伪 ready、没有独立生成的媒体文件。

## 6. A7M4 静态定位结果

A7M4 6.02 `appFw.so`（94 MiB）仍是 stripped，`.dynsym` 只有一个具名定义。
本轮用 ELF 重定位、原指令序列和字符串交叉核对，得到：

| 对象 | A7M4 6.02 ELF VA | 依据 |
| --- | --- | --- |
| `Application_System_Init` | `0x183dbc0` | 唯一具名动态定义 |
| `view::create_screen` 实现 | `0x186a51c` | A7C2 同名函数序言/常量模式：`mov x8,#0x5e08`、`add x1,x25,#0x18c,lsl#12`、`add #0xe48`、`mov x2,#0xfa40`、屏幕数组循环；A7M4 原指令逐条核对 |
| view 分发表 slot 0 | `0x584e0a8` | `.rela.dyn` 中 `R_AARCH64_RELATIVE` 的 addend `0x186a51c`，目标 VA `0x584e0a8` |
| view 分发表基址 | `0x584e098` | slot 0 在基址 +0x10，与 A7C2 的 `0x58dbf28/0x58dbf38` 布局一致 |
| `libgpe.so` | 与 A7C2 字节相同 | SHA-256 `80fc6236379933f80c2689aa002e0cf39483da8d45e876705d2a427cd1a2a7d1` |

这些是**静态地址**，不是“已执行”证明。`view::create_screen` 尚未在 A7M4 的
实际运行中被调用；因此不能把地址命中当成 UI 完成。

## 7. 否定的尝试与未解决问题

2026-11-04 的原线程动态定位、已修复的同步等待与旧判断更正，以
[原 UI 初始化链记录](2026-11-04-original-ui-chain.md)为准。下面保留旧实验现象，
不得沿用已撤销的 CP/presenter 归因。

### 7.1 `im.elf` 的 boot/sequence 等待

在 A7C2 observer 预载实验中，A7M4 `im.elf` 主线程停在：

```
osal_rcv_msg_tmo(queue=0x94016a, timeout=4294967295)
caller=/usr/bin/im.elf+0x2914
```

`im.elf` 在 `0x4025f4` 用 `osal_reg_sync_msg_queue_cb(0x94016a, …)` 注册同步
队列；启动早期另有一个线程向 `0x94016a` 发送 `0x9400e1` 消息，但在注册前被
内核丢弃。**后续反编译更正：该 `0x9400e1` 是已有实例探测，首次启动没有
接收者是正常路径。** 仅丢弃/重放该消息未使应用继续，不能把这条记录当作
丢失 boot 完成消息的证据。`im.elf` 的管理队列等待也不能替代原 AppFw 用户栈。

### 7.2 `liro.ko` 与 CP/PCIe

`liro.ko` 报：

```
liro: Unknown symbol pciatu_unmap (err 0)
liro: Unknown symbol pciatu_map (err 0)
```

A7M4 的 `pcieatu.ko` 存在，但单独加载会在缺 PCIe 模型的板上挂起；`pcie-if.ko`
需要 CP 侧对端。当前 QEMU 板没有 PCIe/CP 功能模型，因此 liro 无法完成加载。
A7C2/FX30 的结论同样适用：`0x92b4086f` 走 public/PCIe 路由，普通 UIPC 队列
反复注册不能替代 CP 对端。

### 7.3 UI 未建屏

默认运行中 `has_create_screen=false`；A7M4 的 `view::create_screen` 地址已
定位但未被调用。尝试用 A7C2 observer 的服务仿真（BusPower、HDMI 0x127
回复、DMM/display info 等）能把 `Application_System_Init` 和 HDMI 请求推进，
但仍未进入 `view::create_screen`，且 A7C2 的多数 appFw 偏移不能直接用于
A7M4。该实验不作为成功路径，也不把 observer 的日志当作 A7M4 原厂 UI 证据。

### 7.4 2026-11-03 继续建屏的否定实验

为了推进到建屏，额外做了以下实验，均未产生 `view::create_screen` 或 GPE 像素：

| 实验 | 操作 | 结果 |
| --- | --- | --- |
| 电源/日期种子 | 在 `upm.ko` 前执行 `seed-power-on 1`，在 `udate.ko` 后执行 `seed-date-clock` | 两项均返回 0（`POWER_ON_RECORD_OK` / `DATE_CLOCK_READY`），仍无建屏 |
| A7M4 `libIMDB` mask | 按 A7C2 的十项硬件服务清低 3 位（USB/Crypt/Hotplug/Tethering/MediaPwrMgr 等） | `Application_System_Init` 仍返回，仍无建屏 |
| imaging pipeline | `MENU_PIPELINE_ENDPOINT=1 MENU_PIPELINE_VIRT=1` | `PIPELINE_ARM` 两个 id 均 rc=0；应用未发送 `0x92840747` 请求 |
| `0x92b4086f` 发送侧回复 | 拦截 `osal_snd_msg`/`osal_snd_sync_msg`，对 1504 B `0x2304` 回 `0x3304`，对 12 B `0x2300` 回 `0x3300`，reply source `0x80c2019f` | tap 看到回复到达 source；应用仍重复 `0x2300`，未建屏 |
| 输入注入 | 直接通过原厂 `osal_snd_msg` 向 `0x5503e0` 注入 ENTER/MENU 按下/释放 | 发送 rc=0，但应用无画面变化；A7M4 的输入队列/消费者可能不同 |
| GPE 最小后端 | 只重定向 A7M4 `libgpe.so` 的五个 worker 槽 | `GPE_MIN PATCH`/`INIT` 成功；没有 `FILL/COPY/FLUSH`，说明建屏前没有绘图 |
| target/dmode | `target=test/resub`、`dmode=1/2/4` | 均无建屏 |
| 直接调用启动画面函数 | appFw 加载后调用 `appFw+0x272e40c` | 进入后不返回、无像素；后续确认首项是 `model_lock`，不能仅凭等待归因 CP |
| appFw 接收队列定位 | tap 记录 `osal_rcv_msg_tmo` 的 tid/queue/caller | 355→`0x81cf6192`、359→`0x81cf6190`、361→`0x802a0116`；反汇编确认是请求/响应服务线程，不是 boot 建屏线程 |
| UI 主循环/SIRCS | 调用 `app_adjchk_ui::main_loop()`，GOT 重定向缺失的 SIRCS 服务 | 到达 `SIRCS_ALLOC`/`SIRCS_ACK`；随后 `presenter_jiritsu` 路径在 libObj 内崩溃（PC≈`libObj+0x133a24`，DABT at `0x30`），仍无 GPE 像素 |
| 新线程重建 UXEngine | 调用原厂 `AppFw` 线程入口 `appFw+0x183dfe8` | 入口执行后 abort；UXEngine/layer 状态不能靠新线程重建，必须使用原线程或 CP 状态 |
| `0x9400e1` 握手 | 反编译 `im.elf` `sub_4038d4` / `sub_402e08` | 这是“已有实例”探测：成功才表示另一进程已注册 `0x94016a`。首次启动失败是正常路径，不是缺的 boot 消息 |
| 旧误标“显示状态” `0x80924127` | 实验回复 `0x920111` 并清零其他字段 | **已撤销**：这是 UPM boot-info；清零 raw BIS 导致原断言，不能作为有效显示服务验证 |
| 远端发送器 | `a7m4_seq.ko` 注册 `osal_reg_public`，只记录、返回 `-1020` | `SEQ_SENDER_REGISTER rc=0`。首批请求含 `0x9092083f`、`0x92b4086f` 的 1504B `0x2304/0x2300`。后者在 `libObj` 是 geo/GPS 发送，接收方 `osal_rcv_msg_tmo` 后仍循环，不是 `view::create_screen` 的门 |

这些否定结果和源码保存在
[`runtime/analysis/experimental/`](../../runtime/analysis/experimental/)。
默认 `./start.sh app` 不加载这些实验；它们不能作为“A7M4 UI 已恢复”的证据。


## 8. 复现

```bash
cd final/a7m4-qemu
./start.sh prepare --bodydata /path/to/BODYDATA.DAT
./build-qemu.sh
./start.sh app --seconds 35
```

`./start.sh shell --command 'uname -a'` 使用 1 vCPU 进入 guest shell；`app` 默认
2 vCPU 并让 init 自动加载模块。2 vCPU 下 PL011 不能交互，这是当前板级模型
的已知限制，不是应用本身的行为。

## 9. 验证边界

| 项 | 状态 |
| --- | --- |
| 官方包身份与源码下载 | 已验证（摘要/清单） |
| 原内核启动 | 已验证（控制台版本行） |
| 原厂模块加载 | 已验证 21 个 `rc=0`；缺失模块按原因排除 |
| 原厂 appFw/UI 引擎 | 已进入 stella、DMM 堆；`Application_System_Init` 仅在 A7C2 observer 实验中返回，日志未保留 |
| 原厂拍摄页面/菜单 | **未验证/未出现** |
| CMOS/镜头/AF | 未实现 |
| 拍照/录像/编码 | 未实现 |
| 虚拟 SD 成片 | 未实现 |
| 物理 ASIC 兼容性 | 未验证 |

## 10. 文件索引

| 路径 | 内容 |
| --- | --- |
| `runtime/analysis/a7m4_import.py` | BODYDATA 解密、分区解包、rootfs/cpio 组装 |
| `runtime/analysis/a7m4_qemu.py` | DTB 改写、固定地址装载、QMP 截图 |
| `runtime/analysis/a7m4_session.py` | 原厂 initrc 模块顺序与 bootin 启动 |
| `runtime/analysis/a7m4_cli.py` | `prepare/shell/app/status` |
| `runtime/analysis/test_a7m4_runtime.py` | 4 项 host 自检 |
| `source/` | QEMU 补丁、dtc、官方源码清单与页面快照 |
| `validation/evidence/` | 本次运行的脱敏摘要、控制台和截图 |

本报告不把“能启动原厂应用”写成“能使用原厂相机”。剩余工作集中在
`0x94016a` boot 消息、CP/PCIe/liro 和 `view::create_screen` 三个前沿。
