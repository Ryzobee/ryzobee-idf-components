# Yoke Voice 0.2.0

English | [Chinese](README_CN.md)

An ESP-IDF audio component for ESP32-S3 boards connected to a CI1302 over UART.
It provides native ESP-GMF input/output interfaces and an optional asynchronous
player for local playback, HTTP/HTTPS playback, WAV recording, and segmented WAV
recording.

The CI1302 protocol implementation is distributed as a precompiled `.a` library.
Its headers are private to the component; applications use the public headers in
`include/`.

## Requirements and wiring

| Item | Supported configuration |
| --- | --- |
| Target | ESP32-S3 |
| ESP-IDF release validation | 5.5.3 (manifest: >=5.5.3, <7.0) |
| GMF Core, IO, and Audio | 1.1.0 |
| GMF Loader | 1.1.1 |
| Example board | 16 MB Flash and Quad PSRAM |
| CI1302 UART | UART1, 921600 baud |
| ESP32-S3 TX | GPIO37, connected to CI1302 RX |
| ESP32-S3 RX | GPIO36, connected to CI1302 TX |
| Console | UART0, 115200 baud |

The CI1302 must run firmware compatible with the audio protocol used by this
component. Record the actual firmware version during hardware validation.
Use `yoke_voice_device_cfg_t` to change the UART configuration. Only one CI1302
device is supported at a time.

The component selects the protocol archive by `IDF_VERSION_MAJOR`:

| Active IDF major | Precompiled archive | Built with |
| --- | --- | --- |
| 5 | `ci1302_protocol/lib/esp32s3/idf5/libci1302_protocol.a` | IDF 5.5.1 |
| 6 | `ci1302_protocol/lib/esp32s3/idf6/libci1302_protocol.a` | IDF 6.2.0 development |

Both variants use Newlib and require `CONFIG_UART_ISR_IN_IRAM=y`.
IDF 5 uses Newlib by default; IDF 6 must set `CONFIG_LIBC_NEWLIB=y`.
Missing variants and unsupported targets or IDF major versions are rejected.
The manifest accepts IDF >=5.5.3 and <7.0. Current release validation uses
IDF 5.5.3; the archive build environments are shown above. Major-version selection does not establish ABI compatibility
with every other minor version. Exact revisions, compilers, and checksums are
recorded in [protocol build metadata](ci1302_protocol/build_info.json).

### Validation scope

Use IDF 5.5.3 for the current release. The retained IDF 6 archive does not by
itself establish validation of every IDF 6 application/dependency combination.
For basic IO without the optional runtime/player, see `tests/target/io_only`.

## Directory layout

```text
include/                  # Public device, IO, runtime, and player headers
src/device/               # Device initialization and persistent receive state
src/io/                   # Native GMF IO, flow control, timeouts, and exclusivity
src/runtime/              # Optional Pool/Loader, session, and task support
src/player/               # Asynchronous player and its internal GMF pipelines
src/support/              # WAV IO, recording files, and optional network support
ci1302_protocol/include/  # Private declarations needed by the protocol archive
ci1302_protocol/lib/      # Precompiled ESP32-S3 protocol library
cmake/                    # Dependency version constraints for bundled examples
examples/                 # Five complete, independently buildable projects
  player_demo/            # Convenient player API and interactive console
  gmf_play_file/          # File playback using native GMF APIs
  gmf_play_http/          # HTTP/HTTPS playback using native GMF APIs
  gmf_record_wav/         # CI1302 Opus decoding and WAV output
  gmf_record_muxer/       # GMF PCM encoder and segmented WAV muxer output
  <example>/main/         # Application initialization, pipelines, and execution
  <example>/resources/    # Audio assets, where required
  <example>/empty_recordings/ # Initial empty recording image, where required
tests/host/               # Packet, network preflight, and WAV file tests
tests/target/io_only/     # Basic IO build and parameter checks
LICENSES/                 # Retained upstream license text
```

## Build and run an example

For the bundled player/pipeline examples, export your ESP-IDF 5.5.3
environment, then enter an example directory, such as
`examples/player_demo`:

```sh
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the board's serial port. The target is already set to
`esp32s3` in each project's `sdkconfig.defaults`.

Use separate build directories and SDKCONFIG files, or separate project copies,
when switching between IDF versions.

The first build downloads the dependencies declared by the component manifests.
All five projects use the bundled baseline dependency constraints. Building the
examples does not require the internal protocol source or the original
development project.

### Configure networking in main.c

For `player_demo` and `gmf_play_http`, edit the macros directly in that example's
`main/main.c`:

```c
#define EXAMPLE_WIFI_SSID "your-ssid"
#define EXAMPLE_WIFI_PASSWORD "your-password"
#define EXAMPLE_WIFI_TIMEOUT_MS 15000
```

The macros and the static `example_network_connect()` function are in the same
file. `app_main()` calls the function once during initialization to connect Wi-Fi
and prepare the clock for HTTPS certificate validation. There is no separate
network configuration file or console command.

If the SSID is empty, initialization logs an error and returns a failure without
attempting a connection. The direct HTTP example returns from `app_main()` when
network setup fails. The player demo reports the error and keeps its local audio
functions available.

SSID, password, and connection timeout are not configured through Kconfig. The
HTTP playback URL remains configurable through `idf.py menuconfig`.

**HTTP playback itself never connects Wi-Fi, reconnects it, or synchronizes the
clock.** It checks the existing network and reports an error when no network is
available. The check requires an up default interface with an IPv4 address; it
does not probe Internet reachability. Network recovery is the application's
responsibility.

### Console controls

The `player_demo` console provides:

| Key | Action |
| --- | --- |
| `1` through `4` | Play a bundled MP3 file |
| `h` | Play the configured HTTP/HTTPS MP3 URL |
| `r` | Record a WAV file for 10 seconds |
| `m` | Record segmented WAV output for 10 seconds |
| `s` | Stop playback or finish recording |
| `p` | Play the latest recording; for segmented output, play its first file |
| `+` / `-` | Adjust playback volume |
| `?` | Show status and available heap memory |
| `F`, followed by `Y` | Explicitly format the recording partition |

The direct HTTP example uses `h` to play and `s` to stop, and supports repeated
playback. The other three direct GMF examples run once after startup; press `s`
to stop early and reset the board to run again.

Direct recording examples use fixed filenames and refuse to overwrite existing
files. Remove the files or initialize the recording partition before repeating
a recording with the same name.

The default muxer slice duration is 60000 ms, so a 10-second recording normally
produces one file. Set the slice duration to 5000 ms in `menuconfig` to observe
segmentation.

### Preserve existing recordings

**A full flash of a recording example writes an empty `recordings` image and
resets existing recordings.** After the initial full flash, use:

```sh
idf.py -p PORT app-flash monitor
```

This updates the application without rewriting the recording image. The initial
partition layout and filesystem images still require a full flash. The examples
do not automatically format a filesystem when mounting fails.

## Add the component to an existing project

1. Copy the entire component directory to
   `<your-project>/components/yoke_voice`.
2. Add `yoke_voice` to your application's `PRIV_REQUIRES` list.
3. Use the example `sdkconfig.defaults` as a configuration reference. Use
   Newlib (implicit on IDF 5, explicit on IDF 6) and UART ISR execution from IRAM,
   and configure the audio conversion
   nodes for 16 kHz, 16-bit, mono output.
4. Configure and mount your application's storage before requesting file
   playback or recording. The component does not modify the host partition
   table or mount filesystems.

The component manifest declares the GMF dependencies. Applications using
LittleFS must also declare `joltwallet/littlefs` and configure their own partition
table, filesystem mounting, and resource images.

A minimal player setup, after mounting the resource filesystem, is:

```c
#include "yoke_voice_player.h"

void app_main(void)
{
    /* Mount the resource filesystem here before requesting playback. */
    yoke_voice_device_cfg_t cfg = YOKE_VOICE_DEVICE_CFG_DEFAULT();
    yoke_voice_device_handle_t device = NULL;
    ESP_ERROR_CHECK(yoke_voice_device_init(&cfg, &device));

    yoke_voice_player_config_t player =
        YOKE_VOICE_PLAYER_CONFIG_DEFAULT(device);
    ESP_ERROR_CHECK(yoke_voice_player_init(&player));
    ESP_ERROR_CHECK(yoke_voice_play_file("/littlefs/test.mp3"));
}
```

For application use, set a completion callback and consider supplying the
recording `check_space` callback. A successful asynchronous request return value
means that the request was accepted; its final result is delivered through the
completion callback.

## Native GMF and convenient player APIs

- **Native GMF:** register CI1302 IO in your own GMF Pool and create pipelines
  using GMF APIs. The four direct GMF examples show node configuration and
  task/event handling without calling the convenient player.
- **Optional runtime:** use the component's default Pool/Loader setup and
  execution support, or retain your application's own GMF task management.
- **Convenient player:** use asynchronous playback and recording calls without
  creating pipelines yourself. Only one session is accepted at a time; requests
  made while busy are rejected rather than queued or allowed to preempt audio.

Device initialization is intended for the application lifetime. The underlying
protocol implementation has no complete device deinitialization operation.
Destroy a runtime only after its sessions and pipelines have finished.

Playback output must be 16 kHz, 16-bit, mono PCM. CI1302 recording input consists
of framed raw Opus packets and must be decoded before PCM processing. Recording
requests currently support durations from 1 to 30 seconds.

Stopping a recording drains already accepted packets before finalizing the
file. Playback completion indicates processing and UART submission, not an exact
measurement of when the speaker finishes producing sound. HTTP cancellation
may wait for a blocking network operation to return.

The component does not provide transparent compatibility with every upstream
GMF example. Examples that directly operate a codec device, expect PCM capture,
or require different formats or storage need adaptation. Full-duplex audio, AEC,
and multi-microphone configurations are outside the current validated scope.

## Tests and hardware acceptance

From the component directory, run the available host tests with:

```sh
bash tests/host/run_host.sh
bash tests/host/run_muxer_host.sh
bash tests/host/run_network_host.sh
python tests/host/test_protocol_selection.py
```

These cover packet parsing, WAV and slice validation, exclusive file handling,
and network preflight behavior. The IO-only target project checks compilation,
public headers, linking, and basic parameter handling with optional modules
disabled.

**Final hardware acceptance is pending user execution.** A successful build or
host test does not establish audio quality or board-level stability.

Record the component package checksum, board revision, CI1302 firmware version,
Flash/PSRAM configuration, UART wiring, ESP-IDF version, and network environment.

| Area | Checks | Result / logs |
| --- | --- | --- |
| Standalone integration | Build and flash from the component package without the internal project | Pending |
| MP3/WAV playback | Quality, speed, volume, and complete beginning/end | Pending |
| Stop and repeat | Repeated playback/stop cycles and successful subsequent requests | Pending |
| HTTP/HTTPS | Startup networking, playback/stop, offline errors, and no implicit reconnection | Pending |
| WAV recording | Full and early-stopped recordings, playback, duration, and file format | Pending |
| Segmented recording | 5000 ms slices, all resulting files, and early stopping | Pending |
| Native GMF | All four direct examples and pipeline node/parameter changes | Pending |
| Busy requests | Rejection of additional requests without disrupting the active session | Pending |
| File errors | Missing files, duplicate names, and insufficient storage without damaging existing files | Pending |
| Stability | Repeated operations, heap trends, errors, and resets | Pending |

Mark each item as passed, failed, or not tested, and retain reproduction steps
and serial logs. Revalidate affected cases after fixes. The user determines
final acceptance based on actual board behavior and audio results.

## License

See [LICENSE](LICENSE) and [LICENSES](LICENSES/) for the retained source license
notices. The CI1302 protocol archive may be used and linked into applications;
the resulting firmware may be distributed in binary form. The protocol
implementation remains proprietary and its source code is not provided.
