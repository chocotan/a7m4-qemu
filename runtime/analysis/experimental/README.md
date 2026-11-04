# A7M4 原线程建屏实验

默认 `./start.sh app` 不加载本目录的实验。原AppFw已自然从`create_screen(0x315)`推进到完整语言选择页`0x12e`；原资源解压及72次GPE COPY实际生成标题、语言列表、选中框及图标。AP帧consumer读取原bitmap实际像素、保存并读回后回复原完成，已观察到帧序号256；11条有限连接、原声效表与显示配置均已消费。新增`--skip-setup`可直接进入原拍摄主界面，实际原像素及帧消费已通过；交互按键、CMOS和拍照录像仍未验证。

[原拍摄UI黑底预览](../../../validation/evidence/2026-11-04-direct-shooting-ui-preview.png) · [原RGBA透明图](../../../validation/evidence/2026-11-04-direct-shooting-ui.png) · [语言页对照](../../../validation/evidence/2026-11-04-original-language-ui.png)。透明图保留原bitmap全部通道；黑底图仅供展示，避免查看器把透明区域衬成白色。`MENU`设置菜单尚无经验证截图。

最新原线程定位与旧判断更正见
[2026-11-04 原 UI 初始化链](../../../report/2026-11-03-a7m4-qemu-bringup/2026-11-04-original-ui-chain.md)。

## 重现当前路径

在 `final/a7m4-qemu` 执行：

```bash
python3 runtime/analysis/experimental/run-ui-trace.py \
  --seed-power --hdmi-observer --updater-close --capid-storage \
  --sa-address --dmm-storage --firmware-identity --display-model \
  --inspect-thread --seconds 60
```

需要跳过首设时，在上述命令追加`--skip-setup`。它通过原API读回并准备`02cf0014=0`、`01070140=0`和触摸教学已读`02cf1701=1`；原日期服务采用经校验的固件默认时钟并验证走时。语言保留官方包已有值。原启动自行选拍摄UI，未直接调用切屏函数。具体依据和失败回滚见[首设跳过报告](../../../report/2026-11-03-a7m4-qemu-bringup/2026-11-04-skip-first-setup.md)。

运行器使用独立 inode 的根文件系统副本，先编译报文自检，再构建 ARM64 载荷。原始 `console.log`、源码快照、运行参数与独立证据 JSON 留在 `menu-runs/ui-trace-<时间>/`。正常 QMP quit 后删除本轮根文件系统与 cpio，并核对默认 preload/session 摘要未变。不再使用会把改动传回基线的硬链接复制。

`--inspect-thread` 仅做原 AppFw / SeqQueueCommon 的寄存器、frame-pointer 链取证；每次 attach 后必须正常 detach。最终自然建屏验收可以省略。QMP `screen.ppm` 可能仍为占位输出，不能仅凭文件存在或非黑像素称为原 UI。

## 当前实现

| 文件 | 行为与判据 |
| --- | --- |
| `ui-trace.c` | 记录实际 OSAL send/receive、来源、原字节和 caller；校验原 vtable slot 值后包装 create_screen；不从另一个线程调用 UI |
| `hdmi-register.c` | 限定原 `2500/0127` 12B 注册，保存真实通知队列 `802a0116`，回 `8127` 完成；不发电缆/ready 通知 |
| `updater-close.c` | 仅关闭没有更新操作的虚拟会话，8B `{4,0}`；不实现固件传输或写入 |
| `seed-setup-complete.c` | 首设及教学原完成记录：精确单字节读取、保存原值、写入读回；任一失败整体恢复并核验，作用限本次虚拟Backup内存 |
| `seed-date-clock.c` | 原udate op2/3/0准备合法boot时钟，拒绝未知状态，验证完整48B回复与实际走时 |
| `seed-power-on.c` | 通过原 Backup API 准备虚拟 loader record，读回全记录核对；原 UPM 的真实 `920111` 返回 bytes 才算枚举验证 |
| `capid-service.[ch]` | 从 FX30 项目的研究代码复用的状态化表存储：地址、访问属主、1024 项表及同步操作；不产生图像或拍摄完成 |
| `capid-adapter.c` | 仅在明确保留 `0x41c200000..0x41c205000` 后映射表；真实队列交付失败回滚表与状态；QMP 独立读回 RAM |
| `display-service.[ch]` | 原 `0169` 几何、`0118` 面板、`013c` 选择、`011a` 亮度、`0158` 掩码、`014b` 布局、`0146` 18项平面表、`0160` monitor LUT、PIN断开及HDMI输出mode；保存请求状态，不生成帧；几何为明确的640×480功能模型 |
| `dmm-service.[ch]` / `dmm-adapter.c` | CP 两个 `0x80000` 显示缓冲有真实保留 RAM，AP 同单元继续由原服务分配；有限单元模型不等于全部 DMM ABI |
| `pin-adapter.c` / `device-models/pin-registry-core.*` | 按完整键查询独立owner目录；仅接受已观测的查询owner=0，回复包含真实对象状态和owner；跟踪原回复释放 |
| `pin-link.c` | 四阶段双端元数据状态转换，消费实际分配的peer请求/回复并读回互指状态；交付失败回滚，格式保持pending，不声明帧ready |
| `device-models/audio-*` / `system-sound-owner.*` / `sound-table.*` | 复用有限音量与声效存储模型；A7M4自身77504B声效资产存于专用RAM并读回，其他音频操作仍显式门控 |
| `thread-inspect.c` | 只读原线程用户寄存器/调用栈并正常 detach；拒绝非法 TID |
| `headless-osd.*` / `osd-adapter.c` | A7M4原framebuffer/bitmap读租约、原VDT配对消息、实际像素保存读回后才回6003/6005；完整帧消费验收独立于原始bitmap非零计数 |
| `resource-lz.c` / `sony-lz77.*` | 校验A7M4原vtable/getter/bitmap锁定ABI，以有边界软件解压器消费原厂资源，不调用缺硬件的LDEC ioctl |
| `bitmap_preview.py` | 只把QMP读回的原640×480 RGBA4444字节转PNG；保留bin摘要和非零计数，不绘制主机控件或文字 |
| `dig-level-device.*` | 原selector3实际消费为无角度样本的虚拟订阅；交付失败回滚，不产生角度或采样ready |
| `gpe-min/` | 原FillRect/BitBlit/StretchRotateBlit/Flush消费原bitmap；未知绘图格式记录并拒绝，原矩形批次仅观测；没有应用绘图请求就没有像素产物 |

`--firmware-identity` 仅把经包摘要校验的实际 model/region 写入最小 FAT，guest RAM1 提供虚拟 `nflasha1`；host/guest 摘要逐轮比对。原 updater 初始化和版本查询读取该镜像与 dat4，不以版本 ACK 桩替代。原bitmap独立读回记录非零字节数，自动导出保留alpha的`bitmap-*.png`和不透明黑底`bitmap-*-preview.png`；全零只算清屏，非零也需检查实际图像内容和帧提交。可单独生成展示图：

```bash
python3 runtime/analysis/experimental/bitmap_preview.py \
  runtime/analysis/menu-runs/<本轮目录>/bitmap-43a7b4100.bin \
  /tmp/a7m4-preview.png --black-background
```

黑底预览按原alpha混合RGB，不绘制文字或控件；原透明PNG仍用于逐字节还原原bitmap。

UPM 查询 `0x80924127 / 0x920111` 现在完全交给原服务。旧“显示状态”应答曾清零 BIS，导致原断言，已经删除。当前 `POWER_ON_RECORD_OK` 加上原 AppFw 接收的 BIS=1 原字节，证明该运行越过了这项错误；不证明整机硬件历史记录已还原。

CAPID 表专用地址刻意与 DMA330 的 `phy_inst_area=0x4102e4000` 分离。不能照搬旧实验的 `0x4102e0000` 表地址：0x4014B 表会与那块 DMA 区域重叠。CAPID 通过仅代表存储消费，不是拍摄验收。

## 保留的历史否定实验

- `call-mainloop.c`、`sircs-fix.c`：曾从新线程调用调整 UI `app_adjchk_ui::main_loop()`，并跳过部分初始化。到达 SIRCS ACK 后崩溃不能证明正常 UI 缺 presenter/CP 对象。本轮确认实际配置为 `AppConfig.so`，不使用这条路径。
- `call-ui.c`：曾直接调用 `appFw+0x272e40c`；该函数先拿 model_lock，其他线程等待锁不等于缺 CP 完成。
- `ep-92b4086f.c`、`ep2-92b4086f.c`、`tap-reply.c`：曾对 `0x2304/0x2300` 构造回复；后续已确认是 `libObj` 的 geo/GPS 请求，不是当前建屏等待。
- `tap-rcv.c`：早期队列/caller 观测代码。`appFw+0x35b8b9c` 实际是同步 send-and-wait 的等待点；不能因队列动态生成就把原 AppFw 线程误判为独立服务线程。
- `seq-sender/`：只捕获内核远端发送器并返回失败，不投递 CP 完成。默认运行器不装载它。

历史运行未保留原日志的结论仅供线索；本轮原线程运行均保留原日志。真正 `boot_start_seq` 为 `0x27960ac`；更早的 `0x2b sequence_cold_boot` 已正常完成。当前推进的是 `sequence_boot::run_i` 的原数据流图，不能把元数据连接等同于其promise完成。
