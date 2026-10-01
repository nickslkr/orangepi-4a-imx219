# Orange Pi 4A / Allwinner T527 (A523) — Sony IMX219 Camera Support

Experimental Linux camera support for the Sony IMX219 on the Orange Pi 4A,
based on the Allwinner T527 / sun55i-A523 platform.

This repository contains the kernel patch series, Device Tree changes,
A523/T527 MIPI CSI-2 receiver D-PHY support, CSI capture backend, userspace
RAW10 processing code, OpenCL debayer implementation, systemd configuration,
reference information, and validation results produced during the bring-up.

## Project status

The camera pipeline is operational on the tested Orange Pi 4A system.

Working hardware path:

    Sony IMX219
        |
        | MIPI CSI-2, 2 data lanes
        v
    A523/T527 MIPI2 receiver / D-PHY2
        |
        v
    Allwinner CSI parser / capture engine
        |
        v
    /dev/video0
        |
        | RAW10 / RG10
        v
    OpenCL userspace debayer + YUV conversion
        |
        v
    v4l2loopback
    /dev/video10
        |
        | YUV420
        v
    uStreamer
        |
        v
    MJPEG network stream

Tested output resolution:

    1920x1080

The stable operating target used for the final userspace pipeline is
approximately 10 FPS.

Measured camera stream rate was approximately:

    9.74 - 9.96 FPS

## Hardware

Test platform:

- Orange Pi 4A
- Allwinner T527 / sun55i-A523 family
- Sony IMX219 camera sensor
- MIPI CSI-2
- 2 data lanes
- IMX219 I2C address: `0x10`
- sensor PWDN: `PH2`
- camera MCLK: `PE15`
- MCLK frequency: 24 MHz

## Kernel

Development and validation were performed against Linux 7.2.6.

Patch-series base:

    500df175a7f9e6bc1a9c328590ca5150f84f9ff0

Base commit subject:

    Linux 7.2.6

The kernel patch series is located in:

    kernel/patches-v7.2.6/

It consists of 22 ordered patches:

    0001 ... 0022

An aggregate diff is also provided:

    0000-full-series-v7.2.6.patch

The series contains work for:

- Allwinner A523/T527 MIPI CSI-2 support
- A523 MIPI CSI-2 receiver D-PHY
- MIPI2 instance handling
- MIPI2 pinctrl
- CSI MCLK pinctrl
- I2C0 camera control bus
- Orange Pi 4A camera Device Tree configuration
- Sony IMX219 Device Tree integration
- sun6i CSI hardware-operation hooks
- variant-specific resource handling
- stream shutdown ordering
- A523 CSI capture backend
- Device Tree bindings for the A523 CSI and MIPI D-PHY
- Orange Pi 4A CSI capture topology

## Patch validation

The complete 22-patch series was tested by applying it with `git am`
to a clean Linux 7.2.6 tree.

Result:

    22/22 patches applied successfully

No patch conflicts occurred.

The resulting tree was then compared with the aggregate patch:

    0000-full-series-v7.2.6.patch

Result:

    EXACT MATCH: 0001-0022 == 0000 aggregate diff

The comparison was byte-for-byte.

The validation record is available at:

    results/kernel-patch-validation-v7.2.6.md

`SHA256SUMS` covers the aggregate diff and all 22 individual patches:

    23 files total

All checksum entries passed `sha256sum -c`.

## Userspace processing

The IMX219 produces Bayer RAW10 data.

The repository contains a custom userspace processing path under:

    userspace/

Important files:

    imx219_ocl_producer.c
    imx219_debayer_yuv420.cl

The producer uses OpenCL through Rusticl/Panfrost on the Mali-G57 GPU to
perform Bayer processing and conversion to YUV420.

The processed frames are written to a v4l2loopback device:

    /dev/video10

Typical virtual-camera format:

    YUV420 / YU12
    1920x1080

uStreamer can then consume `/dev/video10` and expose the camera as an
MJPEG network stream.

Example service files are provided in:

    systemd/

v4l2loopback configuration is provided in:

    config/

## Why the final pipeline uses approximately 10 FPS

20 FPS was investigated but was not achieved reliably with the current
userspace processing architecture.

The limiting factor was not simply the IMX219 sensor or CSI capture path.
The RAW10 userspace path includes frame transfer, OpenCL processing,
debayer/conversion work and GPU-to-system-memory readback/copy overhead.

Measurements during optimization showed approximately 81 ms of userspace
processing cost per 1920x1080 frame in the relevant path, corresponding to
a practical ceiling of roughly 12.3 FPS before additional streaming and
system overhead.

For this reason the final stable configuration was limited to approximately
10 FPS.

This is a userspace processing limitation of the current implementation,
not evidence of a 10 FPS hardware limit of the IMX219 or the CSI receiver.

## Known limitations

This is experimental bring-up code, not an upstream-ready final driver set.

Known limitations include:

- the current 1080p RAW10 userspace processing path does not sustain 20 FPS;
- GPU readback and memory-copy overhead remain significant;
- the current pipeline intentionally targets approximately 10 FPS;
- simultaneous camera workloads can introduce system/resource contention;
- during earlier testing, running the temporary IMX219 processing pipeline
  caused an existing Logitech UVC stream to freeze until the IMX219 test
  pipeline was stopped;
- additional cleanup and review would be required before proposing the
  kernel work for upstream Linux inclusion.

The repository documents a tested hardware bring-up and working experimental
implementation. It should not be interpreted as a claim that the patch set
is already suitable for upstream merge without further review.

## Reference vendor trees

Vendor/reference sources were used to understand the undocumented or
insufficiently documented A523/T527 camera hardware.

Orange Pi reference kernel:

    orangepi-xunlong/linux-orangepi

Reference branch:

    orange-pi-5.15-sun55iw3

Reference commit:

    5b956b750422abd5e9e9c5523540d2aaf41a7939

Avaota reference kernel:

    AvaotaSBC/linux

Reference branch:

    linux-5.15

Reference commit:

    a464bc4feaff7b102dac6362b44ac3303ec7cae1

Additional reference metadata is kept under:

    references/

The vendor sources were used as hardware references while implementing and
testing the Linux 7.2.6 solution. They are not the kernel base to which the
22-patch series is applied.

## Repository layout

    kernel/
        patches-v7.2.6/       kernel patch series and aggregate diff
        current-working-tree/ snapshot of relevant development files

`kernel/patches-v7.2.6/` is the canonical reproducible Linux 7.2.6 patch
series for this project. The ordered `0001` through `0022` patches were
validated against the clean Linux 7.2.6 base described above.

`kernel/current-working-tree/` is a development snapshot of additional
files and uncommitted changes from the system used during bring-up. It is
provided for reference and historical/debugging purposes and must not be
confused with, or applied as part of, the validated 22-patch series.

    userspace/
        OpenCL IMX219 processing implementation

    ffmpeg/
        FFmpeg-related experimental patches

    systemd/
        example producer and streamer services

    config/
        v4l2loopback configuration

    references/
        reference-source metadata

    results/
        reproducibility and validation records

    docs/
        additional project documentation

## Artificial intelligence assistance

Artificial intelligence was used extensively during this project.

ChatGPT by OpenAI was used as an engineering assistant during parts of the
investigation and development process, including:

- analysis of Linux kernel and vendor BSP source code;
- comparison of mainline and vendor camera architectures;
- interpretation of Device Tree, CSI, MIPI CSI-2 and D-PHY configuration;
- debugging assistance;
- development and review of experimental kernel changes;
- development and optimization of userspace processing code;
- OpenCL/Rusticl/Panfrost investigation;
- preparation and organization of patch series;
- generation and review of shell commands used during testing;
- documentation and repository preparation.

AI assistance did not replace hardware validation.

The AI did not have direct access to the Orange Pi 4A hardware. Commands
were executed on the real system by the project author, and the resulting
kernel logs, register information, media topology, capture output,
performance measurements and test results were returned for analysis.

Claims in this repository about working hardware behavior are therefore
based on tests performed on the physical Orange Pi 4A system, not solely
on AI-generated predictions or simulated results.

The author assumes no responsibility for any damage that may result from
the use of this work. Before applying any commands, patches, configuration
changes, or other modifications, verify what they do and that they are
appropriate for your specific system and environment.

## Author

Nick Sl

Public Git identity:

    Nick Sl <335926735+nickslkr@users.noreply.github.com>

## License

Original project material authored for this repository is licensed under the
MIT License. This includes the original material in:

- `userspace/`
- `docs/`
- `results/`
- `references/`
- `systemd/`
- `config/`
- this `README.md`

The MIT License text is available at:

    LICENSES/MIT.txt

This MIT license does not override the licensing terms of third-party or
upstream-derived material.

Linux kernel patches and kernel-derived files under `kernel/` remain subject
to the applicable licensing terms of the Linux kernel and of the upstream
files they modify or derive from.

The FFmpeg patch under `ffmpeg/` remains subject to the applicable licensing
terms of the corresponding upstream FFmpeg code.

SPDX identifiers and upstream copyright/license notices present in individual
files take precedence for those files.
