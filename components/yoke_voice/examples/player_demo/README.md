# player_demo

使用 ESP-IDF 5.5.3，在此目录执行 `idf.py build`、`idf.py -p PORT flash monitor`。
默认 ESP32-S3。详见 [组件快速开始](../../README.md)。

便捷播放器终端演示，含文件/HTTP播放、普通/分片录音、停止和状态。
完整 flash 会初始化空录音分区并重置已有录音；后续保留文件请使用 app-flash。

在 main.c 定义 SSID/密码/超时，初始化阶段调用同文件内的 example_network_connect()。
没有预设时记录错误并保留本地功能；按 h 播放，不再提供 n 分支。HTTP 播放本身不联网，SSID/密码不使用 Kconfig。

发布验证基线为 IDF 5.5.3。
