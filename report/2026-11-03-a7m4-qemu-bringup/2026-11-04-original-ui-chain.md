# A7M4 原 UI 初始化链：原建屏、有限数据流图与声效资源

**原 boot 已自然推进到语言选择页 `0x12e`，完整原厂语言页及实际AP帧消费通过。** 最终`ui-trace-20261104-160330`：7个原资源解压、72次GPE COPY、2次Flush，原bitmap读回614400B、355155B非零；消费者取得原缓冲读租约，保存并读回实际像素后返回原完成，已证明到帧序号256，无异常或consumer失败。此前`0x315`仅清屏和不完整帧均作为历史证据保留。十一条元数据连接与16段实际声效切片已被原线程消费；拍摄主界面、交互按键与成片未验收，`capture_verified=false`。

[完整原语言页PNG](../../validation/evidence/2026-11-04-original-language-ui.png) · [公开内容摘要](../../validation/evidence/2026-11-04-original-language-ui.json)

后续已通过原Backup完成态及原日期API跳过首设，直接进入原拍摄主界面；见[首设跳过报告](2026-11-04-skip-first-setup.md)。本页保留语言页阶段的原始验证边界，后续不覆盖其历史证据。

## 范围与样本

只使用本机隔离 QEMU、更新包中解出的 A7M4 6.02 ELF 和独立根文件系统副本。不连接相机、物理 SD 或物理磁盘，不运行固件更新写入操作。Sony 系统源码归档仍为 6.00，与运行包版本分别记录。

`appFw.so` SHA-256：`1e1b9202eaa69198f20df03dbfdab2f103b206cc4ebcd2e7bd817e6a710018a2`。

两个指定模型子代理以 `jkl/gpt-6.1-sol`、thinking `max` 运行，仅做静态只读调查；主会话核对原字节、实现并运行 VM。原始日志和静态导出保存在忽略分发的 `runtime/analysis/menu-runs/`。本页与源码不包含原固件。

## 必须更正的旧判断

- `0x9400e1` 是已有实例探测。首次启动时没有接收者是正常路径，不能据此判定 boot 消息丢失导致 UI 未建屏。
- `Application_System_Init=0x183dbc0` 创建原线程入口 `0x183dfe8`。入口比较 `AppConfig.so` 与 `appFw+0x5db99a8`；相等走正常 UI，其他配置走 `app_adjchk_ui::main_loop()`。本轮实际读到 `config=AppConfig.so`。此前在新线程强行执行调整 UI 并跳过初始化所产生的 NULL/abort，不能证明正常 UI 缺 presenter/CP 对象。
- 真正的 `model::model_api::detail::boot_start_seq()` 是 **`0x27960ac`**。此前 `0x2230fc4` 标注错误。
- `appFw+0x272e40c` 首先获取 `model::model_lock`（`0x2ee71e8`）。其他线程直接调用会等待锁；仅凭不返回不能证明它在等 CP。
- **`0x80924127 / 0x920111` 是 UPM boot-info 查询，不是显示状态查询。** 旧实验将回复字段清零后，raw BIS 为 0，导致当前已实际观察到的原断言。该应答代码已经撤销。
- `0x920143/0x920145` 在原 `callback_off_cmd=0x3646084` 路由为关机相关 notice，不能直接当作开机序列完成。对应内部事件分别为 `0x0100003b / 0x0100003d`。

## Evidence → Finding → Path

| Evidence | 实测或原字节 | Finding | 下一路径 |
| --- | --- | --- | --- |
| E1 `ui-trace-20261104-130854` | 原 AppFw `osal_rcv_msg_tmo` 等动态队列，caller=`appFw+0x35b8b9c` | 原 UI 仍在前置初始化，尚未进入 `0x5503e0` 主循环 | 定位 `send_and_wait_cmp_i=0x35b8ab4` 的发起请求 |
| E2 `ui-trace-20261104-131250` | 发 `0x909a015a`，12B `{01272500,0001000c,802a0116}`；原 recv 收 `{81272500,0001000c,0}`，source=`0x909a015a`、rc=0 | HDMI inhibit 观察者注册完成已被原线程消费 | 沿原线程继续，不发送伪造电缆/ready 通知 |
| E3 `ui-trace-20261104-131833` | 三次正常 ptrace attach/read/detach；原用户栈含 `libupdaterufp+0x19c0 → +0x35ec` | Bluetooth 的同步装载内部调用 `udtr_ufp_exit`，等待空更新会话关闭 | 仅实现无更新所有权时的 cmd4 幂等关闭 |
| E4 `ui-trace-20261104-132110` | `UPDATER_EMPTY_CLOSED rc=0`，原线程继续启动 UPM、DataService 等线程，随后断言 `infra_wrapper_upm.cpp:361` | 原先两个同步等待已越过；新失败是实验 UPM 回复的非法 BIS | 撤销查询应答，准备虚拟 bootrecord，由原 UPM 回复 |
| E5 原指令 `0x3644204..0x3644238` | raw BIS 只接受 1/2，分别映射 model BIS 0/1；否则调用断言 | 0 不是合法原 BIS 值 | 核验原服务实际返回字节，不能只看 seed 返回码 |
| E6 `ui-trace-20261104-132432` | `POWER_ON_RECORD_OK mode=2 boot_type=1 eecap=1 bis=1`；原 UPM 实际返回 `{00920111,2,1,1,0,1,0,1}`，原 AppFw 自己消费 | BIS 断言消失；原线程进入初始化 future 等待 `0x2706d80` | 采样实际生产该 future 的原工作线程 |
| E7 `ui-trace-20261104-132924` | `SeqQueueCommon → libmpr+0x13423c → appFw+0x318cdac`；发 24B `9004015e / 1000` | 初始化请求 0x2b 是 `sequence_cold_boot`，当前等待 CAPID 表地址 | 状态化表存储与专用 RAM |
| E8 `ui-trace-20261104-133421` | 原 AP 依次消费 `1000 → 1001 → sync action1 → 1002 → 1003`；QMP 读回 20480B，表头 `{1024,1023,1023,1023,0}`，其余表项与保护区全零 | 冷启动 CAPID 存储消费通过，不代表拍摄完成 | 原 worker 推进到 SA 地址转换等待 `libmpr+0xf9d44` |
| E9 `ui-trace-20261104-133811` | 原 MPR 发 `930101/B850`，输入 `0x4fff81000`，收到软件总线别名 `0xfff81000`；worker 随后到 `sequence_cold_boot::init_hdmi` | SA 地址转换被原 MPR 消费，未实现 hash/成像 | 核对冷启动最后两项 HDMI 设置 |
| E10 `ui-trace-20261104-134232` | 原 worker 消费 `200d/032d` 与 `2500/0102` 配置完成；采样显示 Common worker 回到空闲、主 UI 已越过初始化 future，等 `9061003c` 的 unit `63a` 分配 | **原 sequence_cold_boot 已正常完成**；原主线程继续初始化辅助缓冲 | 接有状态 DMM 实际 RAM 分配，不强行唤醒 future |
| E11 `ui-trace-20261104-134927` | 两次 CP Allocate 得到不同句柄 `8400/8401`，各有真实 `80000` RAM；原 AppFw 实际进入并返回 `create_screen(315)`，随后版本查询失败断言 | 首次自然建屏调用已通过，像素和后续启动仍未通过 | 核原版本服务初始化所需文件 |
| E12 `ui-trace-20261104-135640` | 添加包身份派生 dat4=0206 后出现原 GPE FillRect/Flush，但版本端点仍 -1020 | dat4 不是全部资源前置，原 updater mount 失败会注销端点 | 用 guest RAM block 提供最小身份 FAT |
| E13 `ui-trace-20261104-140210` | guest RAM1 FAT 摘要匹配本轮构造值；原同步查询 cmd5 返回 status0/major6/minor2；create_screen ENTER/RETURN 各1，Fill/Flush各1，无异常 | 原版本服务、原建屏及清屏消费通过；bitmap全零，尚无可辨界面 | 真正 boot future 的工作线程在 MediaDetect::SendMessage 等待初始化 |

E3 的顶部 PC 在 libc ioctl，经 `libosal_uipc` 进入等待；其后为 `libupdaterufp+0x19c0`、`udtr_ufp_exit+0x28`、im.elf 同步回调、BluetoothLowEnergy 装载，底部回到原 AppFw。没有在新线程调用 UI。

HDMI 原请求构造器位于 `appFw+0x35bd0bc`；`+0x35bd130` 读取完成包 `+8`，经 `+0x35b7ab8` 转换状态。模型保存真实观察者队列 `0x802a0116`，不生成连接或 inhibit 变化。

更新会话关闭的原调用为 `libupdaterufp+0x35e8 → +0x1928`：8B 请求的 command=4，`+0x19d0` 检查完成包 command 相同，`+0x19e0` 检查 status=0。模型拒绝非关闭操作，不提供传输或写入能力；见 `updater-close.c`。

## 实现与复现

代码直接维护在 [`runtime/analysis/experimental/`](../../runtime/analysis/experimental/)：

- `ui-trace.c`：原队列观测、按原 vtable 值校验后包装 create_screen；HDMI 注册与空会话关闭均为显式开关。
- `hdmi-register.c`：限定原始 12B 请求，保存观察者并构造 0x8127 完成。
- `updater-close.c`：限定空会话的 8B cmd4 关闭。
- `seed-power-on.c`：复用 FX30 项目的研究工具，通过原 Backup API 准备虚拟 loader record，并读回全记录比对；不把描述符当作物理硬件历史值。
- `capid-service.[ch]` / `capid-adapter.c`：复用已存在的状态化表协议，专用虚拟 RAM 为 `0x41c200000..0x41c205000`。动作 1/2/3 和控制 1000..1003 已与 A7M4 原代码核对；恢复动作 4/1005 未实现并保持拒绝。同步回复末字 `0` 表示没有已创建的恢复表，不是通用成功码。
- `sa-address-service.c`：复用受边界检查的虚拟总线地址别名；未验证 A7M4 物理 SA 地址布局，不实现 hash/图像生成。
- `hdmi-config.[ch]`：依据 A7M4 `0x35bc138` / `0x35bcf18` 存储两项冷启动配置，校验完整请求及 0/1 枚举；回复交付失败不提交状态，不生成电缆事件或视频帧。
- `dmm-service.[ch]` / `dmm-adapter.c`：复用带分配、打开、关闭、释放、代数句柄的内存模型；由明确保留的 `0x41c000000` RAM 提供真实 backing。
- `thread-inspect.c`：只读原用户寄存器及 frame-pointer 链；每次采样必须成功 detach。
- `run-ui-trace.py`：独立 inode 复制根文件系统、构建工具、自检报文、保留原日志/源码快照/配置/证据；最后比较基线控制文件摘要，并正常 QMP quit。

```bash
cd final/a7m4-qemu
python3 runtime/analysis/experimental/run-ui-trace.py \
  --seed-power --hdmi-observer --updater-close --capid-storage \
  --sa-address --dmm-storage --firmware-identity --display-model \
  --inspect-thread --seconds 60
```

每轮路径以 `RUN` / `EVIDENCE` 输出为准。`--inspect-thread` 是诊断，可在最终自然运行验收时省略。生成目录中的 `screen.ppm` 仅为 QEMU 当前输出，只有原 create_screen 与实际原 GPE 像素通过后才能作为 UI 证据。

复现器先运行 HDMI、空会话关闭、CAPID 与 SA 地址转换的 C 自检，覆盖非法字节、长度、空指针、状态不应改变及重复注册/关闭。guest 检查器拒绝非法 TID，已实测 `INSPECT_BAD_TID_RC=2`。现代主机头文件将 strtoul 重定向到 `__isoc23_strtoul` 的两次链接失败也保留为失败；检查器现显式绑定固件已有的 `strtoul`，链接已通过。

CAPID 独立 QMP 读回 SHA-256：`4c80bfdbb63c6182f5ed1c453dc481ae1ebe60b66cdefac770b2b946a851ad00`。原 AP 自行分配本地 0x4014B 表，动作 1 上传原头字段；当前无需合成 CaptureID 条目。`1002` 只结束访问，不释放存储；模型不会自行推进 RP 冒充成像消费。

A7M4 官方 CP `DmmCfg.bin`（42976B）中，unit `0x63a` 位于 `0xa72c`，关联 extent `0x700`；`0x209c` / `0x20b0` 两个对应 extent 的大小均为 `0x80000`。这支持虚拟池中两个该大小的缓冲区；不据此宣称后续全部 DMM 单元或实际图像布局已恢复。

## 48B Pin 查询、有限连接和声效资源

最初捕获 `ui-trace-20261104-144840` 的 BB 查询完整键为 **`{type=0,direction=2,index=1,owner=0}`**，destination=`0x9284074a`。后续运行保持同一键；动态回复队列随启动变化，不硬编码队列地址。

| 原报文偏移 | 实际内容 | 模型判据 |
| --- | --- | --- |
| `+0/+4` | 实际回复队列 / `a0a0a0a0` | 本地非零队列及原magic |
| `+8/+12/+16` | `00300001 / 18 / 30` | 一项、48B、内层偏移24 |
| `+20` | 曾为0，也曾为`7f` | 不作为键或成功状态，不清洗原始日志 |
| `+24/+28` | `00001000 / 00020018` | cate=1000、Get=0、内层24B、flags=2 |
| `+32/+36/+40/+44` | `0 / 2 / 1 / 0` | 完整u32 type、direction、index与已观察的query owner=0 |

Get返回56B，`+44`为对象当前state、`+48`为实际owner SID、`+52`为操作状态。首次查询返回state=1（未连接），后续Connect才改变双端状态。未找到键、未知非零query owner、错误长度或尚有未释放回复均不泛化为成功。

`pin-adapter.c` 复用现有Get-only核心，为BB、VEF、VDF、scalar、ADF、DRAWINFO和HDMI保有独立对象。VDF的有限键还核对了A7M4 `appFw+17b2b48..17b2bd8`、`17b33a4..17b3434` 原端点图构造指令；原SID名称映射 `364a588→4c2eaaf` 为VDF。公开代码只保存有限模型常量，不含原CP程序或私有镜像。

`pin-link.c` 用实际OSAL分配的64B peer request、72B peer reply及56B完成回复驱动共享核心的四阶段状态转换。双方校验键和owner，并读回互指状态；原AP实际释放完成回复才单列为消费证据。回复发送失败时恢复双方未连接状态、清pending，保留原请求并锁定故障；不掩盖失败继续报成功。

以下十一条由 `ui-trace-20261104-152807` 原 `SeqQueueCommon` 请求并消费，完成回复均已由原调用方释放，连接故障计数0：

| 源owner / `{type,dir,index}` | 对端owner / `{type,dir,index}` |
| --- | --- |
| BB `9284074a / {0,2,1}` | VEF `905a0048 / {0,1,1}` |
| BB `9284074a / {8,2,0}` | VEF `905a0048 / {8,1,0}` |
| VEF `905a0048 / {0,2,2}` | VDF `90590818 / {0,1,0}` |
| VEF `905a0048 / {0,2,3}` | VDF `90590818 / {0,1,1}` |
| VEF `905a0048 / {8,2,0}` | VDF `90590818 / {8,1,0}` |
| VDF `90590818 / {0,2,1}` | scalar `91670464 / {0,1,4}` |
| VDF `90590818 / {0,2,2}` | ADF `9058003f / {0,1,0}` |
| VDF `90590818 / {6,2,0}` | DRAWINFO `928d0783 / {6,1,0}` |
| VDF `90590818 / {4,2,0}` | HDMI `909a015a / {4,1,0}` |
| VDF `90590818 / {1,3,0}` | OSD `9080003b / {1,3,0}` |
| ADF `9058003f / {5,2,0}` | HDMI `909a015a / {5,1,0}` |

这些是明确的功能模型元数据连接，**不证明原CP/RTOS已运行，也不证明图像格式、数据帧或物理ASIC兼容**。日志保持`format_pending=1,pixels_consumed=0`。VDF与OSD的type1/dir3为显式双向功能连接，只允许这两个owner及index0；两端确实消费状态，不把它提升为物理ASIC格式。该运行已到`run_i+0x2520`附近的数字水平仪订阅等待（`317a5d0→359c1dc→35befbc`），尚未到promise兑现。不把元数据连接计为UI或拍摄完成。

音频配置原生产者也已独立核对：`35bb46c`（325能力、8B→28B）、`35bc6d8`（327声效表、40B→12B）、`35bc5dc`（328路径）、`35bc7a8`（32a分项音量）、`35bba20`（32e总音量）、`35bc818`（33d存储地址、8B→24B）。模型复用有限状态实现；日志中的model operation编号与Sony wire command分开记录。能力owner为明确无音频输入的虚拟profile，未声明麦克风或播放完成。

A7M4自身 `/cp/SysSound.bin` 为77504B，SHA-256=`dae3d18d37b85a22398573f04090a4a95a97052380f4b2b4402a374516399dfb`，FNV64=`805256db54adc91f`。声效owner加载到专用虚拟RAM `41d000000/20000`，逐字节读回并校验尾部全零；虚拟ADF别名为`81d000000`，不是物理硬件历史地址。

`ui-trace-20261104-152246` 中原AP实际提交16段声效表（kind1..16）与分项音量；每段检查真实owner范围和切片摘要。QMP再独立读回RAM，源内容一致且保护区全零。`audio_output_verified=false`；真实声效数据注册不等同于音频输出。

新增C自检覆盖完整查询键、错误owner/长度/缺失键、双端互指、原回复释放以及发送失败回滚；复用音频自检另覆盖资产摘要、声效切片、状态化音量和受损RAM拒绝。`ui-trace-20261104-150312` 的诊断函数名编译失败、`151818` 的复用自检有符号比较警告导致构建拒绝均保留；修复后运行通过，失败轮未启动VM，不改写为成功。

## 原 boot 后段、资源解压与首次可辨认像素

`ui-trace-20261104-153127` 原线程消费数字水平仪24B selector3，复用的虚拟mode2 inline worker实际保存订阅与countdown30；原AP释放回复。没有样本源，`sample_valid=0,angle_events=0`；交付失败回滚订阅状态。原生产者`35befbc`只接受cmd3/4并检查完成`+16`的0..2状态，当前模型严格只支持实际观测的全零保留字段cmd3。

随后Common等待并行显示任务：`3157b38←31760c4←317a95c`，生产者是`31734c8→359a01c→35bafc8`的`vdf_set_panel_reverse`（0119，28B→12B/1119）。原跳转表`4bf9a08`证明仅六种三位组合`000,011,110,010,101,001`（按wire字节8..10次序；跳转表selector0..5）；模型逐字段保存并拒绝其他组合。修复后原任务自然完成，UI越过boot future并在`153430`自然创建/返回`0x12e`。

`153430`保留了一次真实LDEC内核异常：线程`uxengine_res1`，user caller=`libObj+156558`的`ioctl(/dev/ldec,1,...)`，kernel PC=`ffffff8000571f9c`，DABT at0。原LDEC硬件路径没有被误记为通过。后续`resource-lz.c`复用带完整输入/输出边界检查的Sony LZ77解码器，对实际原资源写原bitmap；只改内存中的已校验vtable槽，不修改固件文件：

| A7M4 libObj原地址 | 原字节/ABI判据 |
| --- | --- |
| `25ed20→156310` | Lz77Decoder::decodeImpl虚函数重定位 |
| `156374..156380` | stream对象在decoder+8，原getter为vtable+58 |
| `12f720` | `ldr x0,[x0,#16]; ret`，纯getter；stream+24为原长度 |
| `1563a8→12c840` | 原bitmap写锁，stride输出 |
| `156460..1564a8` | format1/7/8、16像素对齐行宽、实际高度与压缩长度 |
| `1563dc→12c858` | 对应原bitmap解锁 |

`153752`首次解压7个原资源，全部完整消费输入且输出长度、stride及尺寸匹配，无解压失败；原BitBlit执行8次，原bitmap仅出现滚动条。`154212`将原`Gperm+0xa574`的StretchRotateBlit worker同参数ABI也交给有限光栅模型，得到9次实际COPY及“English”文字。无主机文字或控件绘制。

`154212/bitmap-438a16100.bin` SHA-256=`b19f0f50950e05e2697e86072ef6ea5f42ad953c6b711570b0cb1484a42dd472`，614400B，非零311328B。`bitmap_preview.py`只按明确的功能RGBA4444通道转换字节为PNG；红、绿、蓝、透明通道与长度拒绝由独立PNG解码测试验证。`154948`起运行器自动保存原bin及PNG，原bin始终为内容证据。

`154815`的新光栅自检曾拒绝直接复用的FX30夹具。核对发现FX30的libgpe SHA为`f0fb3f7b…`，并非A7M4/A7C2的`80fc6236…`；FX30 completion在+79/A8 selector在+75，A7M4为+7a/+76。**只修正夹具到A7M4已核ABI，未放宽生产解析器。** 该失败保留；后续自检通过。

绘制缺失的否定证据同样保留：`154548`添加原DrawVariousFrames入口追踪，没有收到任何该类型请求；该worker静态上为最多8项矩形边框批次，不是字形批次，目前未实现。已知sprite/copy拒绝会记录原始参数，不以成功回避。原GPE物理格式等价性仍未验证。

历史`154948`主线程在正常`drawFramebuffer`内等待：`libObj+fe05c←f3bc8←f4514←155b9c←170934←appFw1840098`。`154948`采样显示grm_gpe、GPE worker及完成worker均空闲；ExternalOSDIF已消费本地cmd3（6B）与cmd1（48B尺寸配置），然后等下一消息。当时尚未证明原完整帧提交/外部显示消费，未强行signal semaphore。

### 原帧消费最终验收

原`cmd1`会立即向`8280084d`发36B `1003`描述；此前没有追踪这些队列，不能由“未见remote日志”推断未发送。原`f2f88`先向`8280074d`发12B `1002`固定请求，`+8`指定后续`1003/1005`。消费者回复的`6003/6005`由原`f305c`分派，原`f2f00/f300c`自己完成缓冲操作及semaphore通知。

复用`headless-osd.*`的串行消费器，严格核对每对原control/update报文，取得原帧缓冲semaphore、bitmap引用与读锁，并与软件GPE共享光栅互斥锁。实际像素读取、原身份再核、文件写入和逐字节读回全部完成后才投递原完成。失败保留原因并停止，不对后续不完整帧回复。A7M4独立ABI如下；不得套用FX30偏移：

| 项目 | A7M4原地址/布局 |
| --- | --- |
| manager全局槽 | `libObj+2d0580`，由`f37b0→GOT264a48`重定位核准 |
| framebuffer | `manager+8`；on/off指针`FB+68/+70`，只能互为`FB+20/+40` |
| frame身份 | on+0 bitmap、+10 ID、+18完整64位物理地址；FB+8/+a宽高、+c格式 |
| 原FB semaphore ID | `FB+98`；+90是对象基址，原`fe054`取其+8 |
| bitmap vptr | `libObj+254630`，原构造器`f1e8c..f1e9c`核准 |
| AddRef / Release | `fc498 / fc4ac` |
| ReadLock / ReadUnlock | `fc448 / fc45c` |
| Physical / RowPixels / Size | `fc420 / fc61c / fc3ac` |

`osd-adapter.c`解析已核准的原`GRM_bitmap*`导出；AddRef未导出，按已核偏移调用。启动前检查三个队列没有其他消费者且LIRO未装载；一次性claim阻止同代重启。消费器注册process-exit stop/join，单独GRM动态退场场景未测试；当前验收使用正常QMP退出，不注入关闭/强杀。

`160123`第一次集成在API/owner前置检查失败，未启动consumer、未发完成。原因是原libObj位于预加载链的前方，`RTLD_NEXT`不包含这些GRM导出；改为`RTLD_DEFAULT`并核对实际地址后，`160330`正常启动。

**最终60秒原固件综合运行：** 原`create_screen`进入/返回各2次（315、12e），7资源解压成功、0失败；GPE Fill3、COPY72、Flush2。OSD有13条抽样保存/完成日志，最新已观察序号256；抽样记录前8项及随后2的幂，不把13当总帧数。consumer失败0、原异常0。原bitmap SHA-256=`b127de144dbac6d6b031fa020639331efe78992e649098908a218b685a40cbcf`，PNG独立反解并重打包回RGBA4444，全部614400B摘要一致，非零355155B。

PNG内容为原语言标题、五项可见语言、橙色日本語选中框、滚动条及底部图标。原AppFw主循环、原资源与绘图调用负责生成这些内容；主机只导出原字节。QEMU ramfb窗口的呈现另列未实现，不把旧占位画面当原UI。

16项C模型自检及9项Python运行时测试通过。baseline preload/session摘要未变，正常QMP退出后本轮root/cpio已删除。公开目录只保留PNG及白名单内容摘要，固件、私有setting、完整原日志和RAM块仍在忽略分发目录。

## 原 boot 的后续 promise 门

以下地址由静态调查定位；原线程已越过boot等待并自然进入第二屏，静态promise链与额外门的内部每项字段未逐点动态取证：

- `scr0039_boot_controller::process_power_on=0x2141390 → 0x27992a4 → boot_start_seq=0x27960ac`。
- `0x27978c4` 提交 boot 请求，`0x27978d0` 调用 Sony `model::model_future::wait=0x2ee7444`。
- 原 `sequence_boot::run=0x317aacc` 调用 `run_i=0x31780b4`，之后还有 `0x1f03ed8` 的等待门，最后从 `this+0x10` 取 promise 并兑现。
- 额外等待门涉及 `appFw+0x7df84a0`；其原置位者 `0x1f03c1c` 为 remote presenter 初始化，`0x1f03cec` 写 1 并通知条件变量。
- 原 boot 请求提交开关为 `u8[appFw+0x7fc7530]`；若为零，`0x2534210` 直接返回。该值目前没有被修改或强制置位。

历史E13 的原 bitmap 物理地址为 `0x43a7b4100`，QMP 读回 `614400` 字节，SHA-256 为 `34c69899504b36f13e8b22120cf0fd894e61fcd6b046fb8535b79cc491fa3b3f`；全字节为 0，不能称为可见原厂 UI。

原版本服务 `libupdaterufp::udtr_ufp_init` 会先把 `/dev/nflasha1` 以只读 vfat 挂至 `/mnt`，读取 DAT2/DAT3，再读 `/setting/updater/dat4`；缺任一前置时会注销 `004b024e`。本轮最小 FAT 只装经 BODYDATA 摘要校验后的实际 model/region，副本 dat4 为实际版本BCD。host FAT摘要与guest读回按每轮 config 核对，FAT写入器的卷元数据可使不同轮摘要不同。不提供 updater 执行代码，也不伪造版本查询成功。

`view::create_screen`、实际 GPE fill/copy/flush、原物理 bitmap 内容及可见 UI 分别验收；拍照录像 `capture_verified` 保持 false。
