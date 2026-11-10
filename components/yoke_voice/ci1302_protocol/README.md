# CI1302 protocol archives

The protocol is built twice from the same internal source for ESP32-S3:

| Active ESP-IDF major | Archive | Build environment |
| --- | --- | --- |
| 5 | `lib/esp32s3/idf5/libci1302_protocol.a` | ESP-IDF 5.5.1 / GCC 14.2.0 |
| 6 | `lib/esp32s3/idf6/libci1302_protocol.a` | ESP-IDF 6.2.0 development / GCC 16.1.0 |

The outer component CMake selects exactly one archive using `IDF_VERSION_MAJOR`.
An unsupported major, target, or missing selected archive is a configuration
error. It does not fall back to the other archive.

Both archives use Newlib, UART ISR in IRAM, size optimization, and stripped debug
information. IDF 5 uses Newlib by default; IDF 6 requires CONFIG_LIBC_NEWLIB=y.
All builds require CONFIG_UART_ISR_IN_IRAM=y. Public link symbols are retained.
The distribution contains the binary archives and private headers, without the
protocol implementation source. Static archive distribution and debug stripping
do not provide cryptographic encryption or prevent reverse engineering.

`build_info.json` is the variant index. Each variant directory also contains its
own `build_info.json` recording the exact IDF revision, compiler, source hashes,
configuration hash, and archive SHA256. Selection is by major version; actual
build/link validation covers 5.5.1 and the recorded 6.2.0 development revision.
Other minor versions or ABI-affecting configurations require their own checks.

The private headers are not application APIs. Use the component's public device,
GMF IO, runtime, and player interfaces instead.

The archive mapping itself is independent of the optional GMF runtime. The
upstream codec device dependency excludes IDF 5.5.1, so IDF 5.5.1 integration is
validated with the basic IO configuration; full player validation uses IDF 6.2.0.
