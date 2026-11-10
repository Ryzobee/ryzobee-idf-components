# gmf_record_wav

使用 ESP-IDF 5.5.3，在此目录执行 `idf.py build`、`idf.py -p PORT flash monitor`。
默认 ESP32-S3。详见 [组件快速开始](../../README.md)。

直接使用原生 GMF API；pipeline.c 显示组链，runner.c 显示任务/事件和停止清理，不调用便捷播放器。
启动后执行一次，按 s 提前停止，复位重新执行。
完整 flash 会初始化空录音分区并重置已有录音；后续保留文件请使用 app-flash。

发布验证基线为 IDF 5.5.3。
