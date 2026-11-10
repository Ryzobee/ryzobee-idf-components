# Yoke Voice 0.2.0

[English](README.md) | 简体中文

ESP32-S3 + CI1302 的 ESP-IDF 音频组件。提供原生 GMF IO 和便捷异步播放器。
协议实现以 `.a` 分发，允许使用、链接到应用并分发生成的二进制固件，
保留随组件提供的版权和许可声明。协议实现不提供源码；其他源码沿用原有许可证。
详见 [LICENSE](LICENSE)。协议头文件只供组件内部使用。

## 目录

```text
include/                  # 公共设备、IO、运行支持和播放器接口
src/device/               # 设备初始化与持久接收状态
src/io/                   # CI1302 原生 GMF IO，流控、超时及硬件独占
src/runtime/              # 可选 Pool/Loader、会话与任务执行支持
src/player/               # 异步播放器及内部 GMF 组链
src/support/              # WAV IO、录音文件和可选网络辅助
ci1302_protocol/include/  # 静态库所需的内部协议声明
ci1302_protocol/lib/       # ESP32-S3 预编译协议库
cmake/                    # 示例依赖版本约束
examples/                 # 五个可独立构建的完整工程
  player_demo/            # 便捷播放、录音和终端交互
  gmf_play_file/          # 原生 GMF 文件播放
  gmf_play_http/          # 原生 GMF HTTP/HTTPS 播放
  gmf_record_wav/         # 原生 GMF Opus 解码、WAV 输出
  gmf_record_muxer/       # 原生 GMF PCM encoder、WAV muxer 分片录音
  （每个工程的 main/）    # 板级接入、组链和执行代码
  （resources/）          # 文件播放音频资源
  （empty_recordings/）   # 首次运行的空录音分区镜像来源
  （cmake/config 文件）   # 各工程自己的构建、分区和默认配置
tests/host/               # 不依赖硬件的包解析、WAV 文件测试
tests/target/io_only/     # 关闭可选模块后的构建与参数检查工程
LICENSES/                 # 保留的上游许可文本
```

# 快速开始

## 环境与接线

本次发布使用 ESP-IDF **5.5.3** 验证 和 ESP32-S3（当前示例：16 MB Flash、Quad PSRAM）。
先按本机安装方式导出 IDF 环境。默认 UART1：ESP TX GPIO37、RX GPIO36、921600 baud；
终端 UART0 115200 baud。CI1302 需运行支持当前音频协议的固件。
设备配置可通过 `yoke_voice_device_cfg_t` 修改，不支持第二个同时存在的 CI1302 设备。

## 协议库按 IDF 主版本选择

| 当前 IDF 主版本 | 自动选择的协议库 | 编译环境 |
| --- | --- | --- |
| IDF 5 | `ci1302_protocol/lib/esp32s3/idf5/libci1302_protocol.a` | IDF 5.5.1 |
| IDF 6 | `ci1302_protocol/lib/esp32s3/idf6/libci1302_protocol.a` | IDF 6.2.0 开发版 |

只链接对应的一份库，不支持的主版本、芯片或缺失库会在配置阶段报错，不回退到另一份库。
两份库都使用 Newlib、UART ISR in IRAM、大小优化和调试信息剥离，保留必要链接符号。
分发包不含协议实现源码；预编译与剥离调试信息不等于密码学加密。
`ci1302_protocol/build_info.json` 是索引；每份库目录下也有独立版本、源码指纹和 SHA256 记录。
实际构建/链接验证版本是 5.5.1 和所记录提交的 6.2.0 开发版，其他小版本的 ABI 兼容性需自行验证。
切换 SDK 时请使用独立构建目录和 SDKCONFIG 文件，或使用独立工程副本。

## 本次发布的验证范围

组件清单要求 IDF >=5.5.3、<7.0，当前发布验证环境为 IDF 5.5.3。
IDF 6 协议归档保留，不代表所有 IDF 6 应用及依赖组合都已复验。
仅需基础 IO 时，可参考 `tests/target/io_only`。

## 运行自带示例

完整播放器/组链示例使用 IDF 5.5.3。解压组件后，进入任意一个 `examples/<示例名称>`：

```sh
idf.py build
idf.py -p PORT flash monitor
```

默认目标已在 sdkconfig.defaults 中设置为 esp32s3。首次构建需要下载清单声明的依赖。
五个工程均使用组件内固定的基线依赖约束，首次构建无需内部源码工程。
需要网络的 `player_demo` 和 `gmf_play_http` 均提供 `main/main.c`：

```c
#define EXAMPLE_WIFI_SSID "你的SSID"
#define EXAMPLE_WIFI_PASSWORD "你的密码"
#define EXAMPLE_WIFI_TIMEOUT_MS 15000
```

配网宏和静态函数 `example_network_connect()` 均放在 `main/main.c`；在 app_main 初始化阶段调用一次，连接 Wi-Fi 并准备 HTTPS 时间。
启动时连接网络，连接成功后按 **h** 播放；没有 n 命令。SSID 为空时打印“未预设网络”并返回错误，不尝试连接。
直接 HTTP 示例初始化失败后返回；播放器演示记录错误并继续提供本地功能。
SSID、密码和连接超时不再通过 Kconfig 配置；HTTP URL 仍可在 menuconfig 配置。
HTTP 接口只检查已有网络，未联网按 h 提示“没有网络”，不会顺带配网。
设备初始化、HTTP 播放接口和组链函数均不隐式联网、重连或校时。

`player_demo`：1～4 播放 MP3，h 网络播放，r 普通录音，m 分片录音，s 停止，
p 回放最新录音，+/- 音量，? 状态，F 再 Y 格式化录音区。
直接 HTTP 示例使用 h/s 命令，可重复播放；其余三个直接组链示例启动后运行一次，s 提前停止，复位再次执行。
直接录音示例使用固定文件名，重复录音不会覆盖已有文件；需要清除文件或重新烧录空录音镜像。
默认分片间隔 60000 ms，录音 10 秒只有一片；设置 5000 ms 观察分片。

**首次录音示例会烧录空 recordings 镜像，完整 flash 会重置已有录音。**
需要保留录音时，后续使用 `idf.py app-flash` 更新应用；资源和分区首次初始化仍需完整 flash。
示例只挂载分区，不在挂载失败时自动格式化。

## 接入已有工程

将整个组件目录复制到 `<你的工程>/components/yoke_voice`。
应用 CMake 的 PRIV_REQUIRES 添加 `yoke_voice`。组件的 manifest 自动声明 GMF 依赖；
使用 LittleFS 的应用另外声明 `joltwallet/littlefs`，自行配置分区、挂载和资源镜像。
IDF 5 默认使用 Newlib；IDF 6 设置 `CONFIG_LIBC_NEWLIB=y`。两者都设置 `CONFIG_UART_ISR_IN_IRAM=y`；开启转换节点并设置
16 kHz、16 位、单声道。可参考示例 sdkconfig.defaults；组件不会修改宿主分区表。

```c
#include "yoke_voice_player.h"
yoke_voice_device_cfg_t cfg = YOKE_VOICE_DEVICE_CFG_DEFAULT();
yoke_voice_device_handle_t device = NULL;
ESP_ERROR_CHECK(yoke_voice_device_init(&cfg, &device));
yoke_voice_player_config_t player = YOKE_VOICE_PLAYER_CONFIG_DEFAULT(device);
ESP_ERROR_CHECK(yoke_voice_player_init(&player));
ESP_ERROR_CHECK(yoke_voice_play_file("/littlefs/test.mp3"));
```

应用先挂载文件系统；生产应用建议设置完成回调，以及录音 `check_space` 预检查。
便捷接口返回 ESP_OK 只表示受理；完成结果由回调给出。

# 用户硬件验收记录

状态：**待用户执行，尚未通过最终验收**。

记录：组件包 SHA256、板卡版本、CI1302 固件版本、Flash/PSRAM、UART 接线、IDF 版本、网络环境。
首次运行步骤见上文“快速开始”。完整 flash 重置示例录音区；需要保留文件时使用 app-flash。

| 项目 | 操作及观察 | 用户结果/日志 |
| --- | --- | --- |
| 独立接入 | 从组件包解压构建、烧录，确认不需要内部工程 | 待测 |
| MP3/WAV | 播放资源及录音，听音质、速度、首尾完整性和音量 | 待测 |
| 停止与重复播放 | 多次播放/停止，确认能再次受理，无异常重启 | 待测 |
| HTTP/HTTPS | 应用先联网，播放/停止；未联网直接报错，断网不自动重连，应用恢复网络后再次播放 | 待测 |
| 普通录音 | 录制10秒、提前停止、回放；导出WAV检查长度和格式 | 待测 |
| 分片录音 | 间隔5000ms，录制10秒，检查所有片及提前停止 | 待测 |
| 自行组链 | 运行四个直接GMF示例，修改节点或参数评估扩展体验 | 待测 |
| 忙时请求 | 播放期间再次播放/录音，确认拒绝且当前操作可结束 | 待测 |
| 文件异常 | 缺失文件、重名录音、空间不足，确认不覆盖/误删已有文件 | 待测 |
| 稳定性 | 连续操作并记录次数、堆内存趋势、错误及日志 | 待测 |

每项记录通过/失败、实际步骤与日志。发现问题后修复并复验受影响项，由用户给出最终效果结论。
