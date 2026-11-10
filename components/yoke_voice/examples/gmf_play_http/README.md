# gmf_play_http

使用 ESP-IDF 5.5.3，在此目录执行 `idf.py build`、`idf.py -p PORT flash monitor`。
默认 ESP32-S3。详见 [组件快速开始](../../README.md)。

在 `main/main.c` 宏定义 SSID、密码和连接超时；不使用 Kconfig 配网。
`main/main.c` 内的静态函数 `example_network_connect()` 在 app_main 初始化阶段调用一次。
启动时配网（含 HTTPS 校时），未预设 SSID 或连接失败时打印错误并返回。终端按 h 播放，按 s 停止，没有单独配网命令。
HTTP 播放本身不连接、重连或校时；未联网按 h 提示“没有网络”。
HTTP URL 仍通过 menuconfig 的 GMF HTTP example 设置。

pipeline.c 展示原生 GMF 组链，runner.c 展示任务/事件及停止收尾，不调用便捷播放器。

发布验证基线为 IDF 5.5.3。
