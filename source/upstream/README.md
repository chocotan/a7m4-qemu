# Sony ILCE-7M4 Ver.6.00 官方源码包

本目录保存 Sony 官方页面列出的全部 40 个源码包（约 400 MiB）。它们不进入
公开仓库；按 `urls.txt` 下载后运行：

```bash
wc -l urls.txt
xargs -n1 -P4 curl -fLSsO --retry 3 --retry-delay 2 < urls.txt
sha256sum -c SOURCE-SHA256SUMS
```

页面快照和使用须知在上一级：`../ILCE-7M4_Ver600.html`、`../notice.html`。
`linux-kernel.tar.gz` 是 4.9.337，仅用于事实核对；本运行时直接使用更新包
`BODYDATA.DAT` 中的原厂内核，不编译 Sony 内核。
