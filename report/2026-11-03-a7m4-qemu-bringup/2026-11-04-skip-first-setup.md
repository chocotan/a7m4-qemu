# A7M4：跳过首设直接进入原厂拍摄 UI

`--skip-setup` 已在原 ILCE-7M4 6.02 AP 上验证：正常启动创建 `0x315 → 0x355 → 0x351 → 0`，没有语言页 `0x12e` 或触摸教学页 `0x1e9`。原 bitmap 显示 `NO CARD`、`J-FINE 14M`、`AF-S` 和拍摄控件，原 AP 帧消费者正常保存并读回像素后回复完成。

[查看原拍摄 UI PNG](../../validation/evidence/2026-11-04-direct-shooting-ui.png) · [机器可读验收摘要](../../validation/evidence/2026-11-04-direct-shooting-ui.json)

只验证拍摄界面显示及首设分支。取景区域仍黑色；CMOS、镜头、AF、按键交互、拍照录像和虚拟卡成片没有因此通过，`capture_verified=false`。PNG 直接转换 QMP 读回的原 bitmap；QEMU ramfb 窗口呈现仍是独立未验收项。

## 复现

在 `final/a7m4-qemu` 执行：

```bash
python3 runtime/analysis/experimental/run-ui-trace.py \
  --seed-power --hdmi-observer --updater-close --capid-storage \
  --sa-address --dmm-storage --firmware-identity --display-model \
  --skip-setup --seconds 50
```

运行器将原根文件系统复制成独立 inode，使用原 `Backup` 和 `udate` API 在 guest 启动前准备状态。`Backup_save=0` 将设置写入限制在这次虚拟机的内存；不修改原 `BODYDATA.DAT`、原 `Backup.bin`，也不连接真机或物理 SD。

省略 `--skip-setup` 仍可重现原语言页。语言保留官方包已有值，本次没有写语言、隐私或配对设置。原日期采用固件自己的 UTC 默认值，实测为 `2021-01-01`；它不是主机当前日期。

输出目录由 `RUN`/`EVIDENCE` 指示。每轮保留 `console.log`、`config.json`、源码快照、原 `bitmap-*.bin`、直接转换的 PNG 和 `evidence.json`；失败另存 `failure.json`。正常 QMP 退出后删除本轮临时 root/cpio，并检查基线 preload/session/Backup 摘要。

## 原首设状态与实现

样本 `appFw.so` SHA-256：`1e1b9202eaa69198f20df03dbfdab2f103b206cc4ebcd2e7bd817e6a710018a2`。下列地址均为这份 A7M4 ELF 相对地址，未使用 A7C2 的函数偏移。

| 项目 | 原依据 | 本次准备值 |
| --- | --- | --- |
| 首设请求主记录 `02cf0014` | `364c098..364c0a8` 调 `Backup_nread(id,buffer,1)`；`2e73f44..2e73fc8` 将原读回字节装入模型 `1bf3` | 单字节 `0` |
| 镜像记录 `01070140` | 原 setter `364c0ac..364c0e8` 把同一个字节依次写入两个完整 ID | 单字节 `0` |
| 首设完成 producer | `351af50..351afac`、`34e81c8..34e8224` 将模型 `1bf3` 写为 `0` | 与原完成路径同值 |
| 教学已显示记录 `02cf1701` | 名称表为 `BKID_APP_IS_DISPLAYED_ICON_TOUCH_FUNCTION_GUIDE_SCREEN`；原 Backup 目录 `3085968..3085998` 构造完整 ID 并绑定该字段，guest 原 API 确认长度为 1 | 单字节 `1` |
| 教学 OK producer | `23ae1a8..23ae27c` 将模型 `2483` 写为单字节 `1`，提交后请求返回 `351`；getter=`2921474` | 与原 OK 完成路径同值 |

`process_power_on=2141390` 的原首设分支使用模型字节 `1bf3` 和日期状态：`force || date_status==2` 才进入相应首设链。正常分支选择屏 `355`。`1c14b6c(321)` 是统一功能门，不是 Backup ID；它只在已经进入首设链后决定语言页，本次没有修改该门或语言可用项数量。

[`seed-setup-complete.c`](../../runtime/analysis/experimental/seed-setup-complete.c) 先读取三个字段的精确长度及原值，只接受已经核对的 `0/1` 域，记录原值，再逐项写入并读回。任一写入、读回或内容比较失败，恢复三个原值并再次读回；回滚失败有独立非零返回码。失败时不启动原应用。它没有写任何 appFw RAM 全局，也没有直接调用 UI 构造或切屏函数。

首次验证只清首设请求两字节，原屏序列为 `315→355→351→1e9`，最后显示原触摸滑动教学。这证明语言/日期首设已跳过，但不能当作直接进入无遮挡拍摄 UI。核对原教学完成记录后，加入第三字节，第二轮才直接显示拍摄控件。两轮证据均保留。

## 原时钟准备与读回

复用 [`seed-date-clock.c`](../../runtime/analysis/experimental/seed-date-clock.c)，并用 A7M4 `udate.ko` 原指令核对消息端点 `0090013d`。48B 消息中的 `+16` 为 operation、`+18` 为处理器响应、`+20` 为日期、`+32` 为日期类型、`+36/+38/+40` 为时区字段、`+44` 为设置原因。

流程为：读当前日期 op2；仅当原状态为 `3 / NO DATA` 时读固件默认 op3；输入类型用原 `1 / UTC`、原 boot 原因 `4` 调 op0；再次读 op2，并验证日期和原计时源实际前进。已有合法时钟保留；`2 / UNSET TIME` 与其他未知状态均不自动覆盖。

原注册和分派位于 `udate.ko+.text+17c8..17ec`、`15a8`；boot 原因4进入 `c10`，读当前在 `1680..168c`。AP 原转换函数 `363f4d8` 按原枚举保留 `LOCAL TIME/UTC/UNSET TIME/NO DATA=0/1/2/3`。种子不另写日期模型，不把 NO DATA 和 UNSET TIME 混称。

最终 guest 实际读回：

```text
DATE_CLOCK_READY source=firmware-default date=2021-01-01T00:00:02 elapsed=2
SETUP_SEED_READBACK id=0x2cf0014 bytes=1 value=0
SETUP_SEED_READBACK id=0x1070140 bytes=1 value=0
SETUP_SEED_READBACK id=0x2cf1701 bytes=1 value=1
```

完整48B日期回复保存在 console 和公开验收摘要。原字节：

```text
3000000000000000000000000000000002000000e5070101000002000000000000000000000000000000000000000000
```

## Evidence → Finding → Path

| Evidence | Finding | 后续路径 |
| --- | --- | --- |
| 原首设 getter/setter、完成 producer 与精确宽度 | 两个镜像字节的完成状态为0，不需要修改语言功能门 | 原 Backup API 准备并读回 |
| `ui-trace-20261104-162658` 原日期前进、两字段读回0、原屏 `315→355→351→1e9` | 首设已跳过；触摸教学是独立提示 | 核原教学 OK 的完成记录 |
| 原教学完成 `2483=1` 及完整 Backup 目录 `02cf1701` | 可在虚拟设置中保有“已显示过”状态 | 同一事务写第三字段并读回 |
| `ui-trace-20261104-163258` 原屏 `315→355→351→0`，无12e/1e9 | 原开机控制器自然进入拍摄 UI | 检查原 bitmap 和实际帧消费 |
| 原 bitmap PNG 独立反解后与全部614400B一致；原 OSD 持租约保存后回复，已观察序号256 | 真实原 UI 像素和帧消费通过，非主机重绘或空 ACK | 成像、输入和虚拟卡继续分别验收 |

最终50秒运行：原 `create_screen` 进入/返回各4次，资源解压21次成功、0失败，GPE Fill17/COPY110/Flush17；11条有限连接完成及回复释放，连接故障0；OSD consumer失败0、原异常0。

截图源 `bitmap-43a7b4100.bin` 为614400B、24226B非零，SHA-256=`a5c7fe0103d7662fe23c0bde3c0190512e20393ae227ad234425c0a00e830a72`。黑色取景区不被记作 CMOS 帧。另一个原 bitmap 同时保留在本机原运行目录。

本次18项C模型/协议自检和9项Python运行时测试通过；种子测试包括错误长度、短读、非法枚举、第二或第三项交付失败后全项恢复，日期测试包括无处理器响应、错误默认日期、不走时及已有合法时钟保留。PNG 的独立解码逐像素恢复为原RGBA4444字节；基线控制文件未变，临时 root/cpio已清理。
