# RootMaker ESP-IDF Component Library

This repository contains ESP-IDF components dedicated to the **RootMaker
development board (ESP32-S3)** and its peripherals. Each peripheral is packaged as an independent component with its
own example project under `examples/`, and every component can be published
independently to the [ESP Component Registry](https://components.espressif.com/).

The root-level `main` directory contains an LVGL-based touchscreen integration
demo for testing the peripheral components.

The component manifests currently omit `targets`; the board name is documented
in `description` and each component README. Without `targets`, Component Manager
does not filter dependencies by chip. This library remains dedicated to the
RootMaker development board.

## Components

| Component | Description | Registry name |
| --- | --- | --- |
| [`rootmaker_utils`](components/rootmaker_utils/README.md) | Shared interface-port model and I2C bus pool | `ryzobeezero/rootmaker_utils` |
| [`yoke_ec11`](components/yoke_ec11/README.md) | EC11 encoder, button, and two onboard WS2812 LEDs | `ryzobeezero/yoke_ec11` |
| [`yoke_keyw`](components/yoke_keyw/README.md) | Button and GPIO indicator LED module | `ryzobeezero/yoke_keyw` |
| [`yoke_motor`](components/yoke_motor/README.md) | Brushed DC motor driver using MCPWM | `ryzobeezero/yoke_motor` |
| [`yoke_rgbw`](components/yoke_rgbw/README.md) | External RGBW WS2812 LED strip | `ryzobeezero/yoke_rgbw` |
| [`yoke_rad60`](components/yoke_rad60/README.md) | RAD60 UART radar module | `ryzobeezero/yoke_rad60` |
| [`yoke_screen`](components/yoke_screen/README.md) | ST7789 display, CST816T touch controller, and LVGL | `ryzobeezero/yoke_screen` |
| [`yoke_zxacc_maker`](components/yoke_zxacc_maker/README.md) | ZXACC-maker PY32 power-management baseboard | `ryzobeezero/yoke_zxacc_maker` |

Each component follows this directory structure:

```text
components/<name>/
├── CMakeLists.txt        # Component build definition
├── idf_component.yml     # Version, dependencies, and registry metadata
├── Kconfig               # Component options (where applicable)
├── README.md
├── CHANGELOG.md
├── LICENSE
├── include/              # Public headers
├── src/                  # Implementation
└── examples/             # Example projects included in the package
```

The full component set currently requires ESP-IDF 5.5.3 or later because EC11
uses the runtime I2C address-change API. Each component README documents its
usage and API semantics in English. Peripheral drivers acquire their own resources;
do not pre-claim their ports. Serialize lifecycle calls and stop application
access before cleanup. Screen SPI signals are exclusive; touch and power
baseboard I2C can share the same SDA/SCL pair.

Use read outputs only after a successful return, except for the explicitly
documented RAD60 unhealthy-communication snapshot. After successful cleanup,
set the handle to NULL; retain a failed cleanup handle only for retry. Stop
business operations once cleanup starts. The screen owns its returned LVGL
display; applications must not delete it themselves.

Standalone examples may all default to P1. Assign separate ports when combining
peripherals, for example EC11 on P1, radar on P4 and RGBW on P3. The screen and
power baseboard share `rootmaker_sys_i2c_port`.

For API usage, see each component's README.

## Component conventions and application responsibilities

* NULL handles and invalid arguments return `ESP_ERR_INVALID_ARG`.
  Unavailable instances return `ESP_ERR_INVALID_STATE`.
* Applications serialize lifecycle calls, stop instance access before cleanup,
  and decide whether to retry or abort after an error. See the official
  [I2C thread-safety rules](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/peripherals/i2c.html#thread-safety).
* Initialization returns NULL on failure. If rollback fails too, the next init
  retries cleanup before creating an instance. For the singleton screen, call
  `yoke_screen_deinit()` when its log requests a cleanup retry.
* Public-header dependencies use `REQUIRES` and `require: public`;
  implementation-only dependencies use `PRIV_REQUIRES`.
* Each public definition has one name. Update callers when renaming it.

## Integration project pins

| Function | Default configuration |
| --- | --- |
| EC11 I2C | SDA=GPIO15, SCL=GPIO16 |
| RGBW LED strip | Default P1, GPIO15, 3 LEDs |
| RAD60 UART | Default P1, UART1, TX=GPIO15, RX=GPIO16, 921600 bit/s |
| ZXACC-maker I2C | SDA=GPIO41, SCL=GPIO40, address `0x73` |

> The standalone radar example uses RS_PORT1 (GPIO15/16), which conflicts with
> the EC11 example. The root integration project's port selector defaults to P1;
> assign different ports before starting multiple peripherals. Active ports are reserved.
> Driver signal pins are selected by the supplied port descriptor.

## Publishing to the ESP Component Registry

`.github/workflows/upload-components.yml` uses
[`espressif/upload-components-ci-action`](https://github.com/espressif/upload-components-ci-action)
to upload all eight components from [`Ryzobee/ryzobee-idf-components`](https://github.com/Ryzobee/ryzobee-idf-components)
to the `ryzobeezero` namespace:

1. Update `version` in the relevant component's `idf_component.yml` using
   semantic versioning.
2. Commit and push the changes, then create and push a semantic-version tag,
   for example:

   ```sh
   git tag v1.0.0
   git push origin v1.0.0
   ```

3. GitHub Actions starts the upload. Each component version is read from its
   own `idf_component.yml`. Versions already present in the registry are
   skipped, so a tag can release only the components whose versions changed.

Before the first release from this repository, sign in to
[components.espressif.com](https://components.espressif.com/) and open
**Permissions → ryzobeezero → each component → Trusted Uploaders**. Add
`Ryzobee/ryzobee-idf-components` as the trusted repository for all eight components (create any
missing component entries first). The workflow uses GitHub OIDC with
`id-token: write`; no API-token secret is required. For the tag-based release
workflow, leave the optional Branch and Environment restrictions unset.
Existing trusted-uploader entries for the old repository do not authorize this
repository.

## Build and flash the integration project

After loading the ESP-IDF environment, run:

```sh
idf.py build
idf.py -p /dev/ttyACM1 flash monitor
```

Board and LVGL settings are maintained in `sdkconfig.defaults`. ESP-IDF generates
`sdkconfig`, resolves dependencies and creates build output locally; generated
configuration, dependency lock files, downloaded components and build artifacts
are excluded from version control.
The integration demo pins its external dependencies to the versions used for
hardware validation. Published component manifests retain their compatibility
ranges for use in other applications.

For standalone usage, see the project under each component's `examples/`
directory.
