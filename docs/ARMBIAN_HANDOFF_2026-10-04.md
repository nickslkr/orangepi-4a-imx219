# Armbian handoff — Orange Pi 4A / Allwinner T527 (A523) / Sony IMX219

Date: 2026-10-04

## Purpose

This document is a technical handoff for the experimental Orange Pi 4A camera bring-up that enables a Sony IMX219 through the Allwinner T527/A523 camera path.

The goal is to give Armbian and upstream-oriented developers a reproducible starting point. This is not a request to merge the repository as-is. The validated Linux 7.2.6 patch series is kept separate from later development follow-ups.

Project repository:

https://github.com/nickslkr/orangepi-4a-imx219

## Hardware

- Board: Orange Pi 4A
- SoC: Allwinner T527 / sun55i-A523 family
- Sensor: Sony IMX219
- Sensor I2C address used by the working setup: 0x10
- Camera MCLK: PE15, 24 MHz
- Sensor power-down GPIO: PH2
- CSI-2 configuration used by the working implementation: 2 data lanes

Working capture topology:

    IMX219
      -> A523/T527 MIPI2 receiver / D-PHY2
      -> Allwinner CSI parser / capture engine
      -> /dev/video0
      -> RAW10 / RG10 1920x1080

Userspace production topology:

    /dev/video0 RG10
      -> OpenCL debayer/conversion on Mali-G57 via Rusticl/Panfrost
      -> YUV420
      -> v4l2loopback /dev/video10
      -> uStreamer

The stable production target used during validation is about 10 FPS at 1920x1080.

## Initial problem

The stock/mainline-oriented Armbian state already had the generic IMX219 sensor driver, but the complete T527/A523 camera path was not available for this board.

The missing work was not limited to a sensor node. The bring-up required A523/T527-specific support and integration for:

- MIPI CSI-2 receiver
- receiver D-PHY
- CSI capture backend
- resource/clock/power handling
- pinctrl and camera MCLK
- I2C0 camera control bus
- Orange Pi 4A Device Tree graph
- IMX219 board integration

## Validated Linux 7.2.6 patch series

The repository contains a canonical 22-patch series under:

    kernel/patches-v7.2.6/

Base:

    Linux 7.2.6
    500df175a7f9e6bc1a9c328590ca5150f84f9ff0

The series was validated by applying patches 0001 through 0022 with git-am to a clean Linux 7.2.6 tree.

Validation result:

- 22/22 patches applied successfully
- no conflicts
- aggregate patch matches the ordered series
- SHA256SUMS covers the 22 patches plus the aggregate patch

Validation record:

https://github.com/nickslkr/orangepi-4a-imx219/blob/main/results/kernel-patch-validation-v7.2.6.md

Patch directory:

https://github.com/nickslkr/orangepi-4a-imx219/tree/main/kernel/patches-v7.2.6

The exported patch headers use the GitHub noreply address. No private email address is intentionally included in this handoff.

## What is working

On the tested Orange Pi 4A system:

- IMX219 is present in the media graph
- MIPI CSI-2 link is enabled
- sun6i CSI capture entity is present
- /dev/video0 is created
- 1920x1080 RG10 / RAW10 capture is available
- a custom OpenCL userspace producer converts RAW10 to YUV420
- the converted stream is exposed through v4l2loopback and uStreamer

The diagnostic snapshot from 2026-10-04 records the working media graph and /dev/video0 state.

## Important follow-up: sun6i-csi active-state initialization bug

A separate issue remains around V4L2 subdev active-state initialization.

On cold boot / early service startup, the CSI bridge SOURCE pad can remain uninitialized while the SINK pad has a valid default format. This produces an invalid media-bus code 0x0000 on the source side and causes capture startup failures. The kernel log repeatedly shows:

    sun6i-csi 5800800.csi: unsupported bridge format 0x0000

The root cause is in the generic sun6i-csi bridge active-state initialization path: the bridge initializes only the sink pad in init_state. Because this bridge cannot transcode, source format must mirror sink format.

A local development follow-up propagates sink format to source in set_fmt, and the intended cleanup is to initialize both pads consistently in init_state as well.

This active-state fix is deliberately treated as a follow-up and is not presented as part of the already validated 22-patch camera-enablement series.

Detailed note:

https://github.com/nickslkr/orangepi-4a-imx219/blob/main/docs/SUN6I_CSI_ACTIVE_STATE_BUG.md

## Linux 7.2.9 / 7.3 status

The tested hardware snapshot is based on Linux 7.2.6 plus the project changes.

Linux 7.2.9 was inspected for this specific active-state issue. The relevant sun6i-csi state initialization still does not provide the complete both-pad initialization needed here, so merely moving the project from 7.2.6 to 7.2.9 is not expected to solve the SOURCE=0x0000 problem by itself.

The agreed next validation target is the Armbian/Linux 7.3 generation:

1. rebase/inspect the camera series against the newer kernel,
2. verify the sun6i-csi active-state conversion and both-pad initialization,
3. verify cold-boot camera startup without userspace format priming,
4. only then prepare a cleaned submission/PR.

No Armbian source code is modified by this publication.

## Development snapshot versus canonical patch series

The repository intentionally separates:

- `kernel/patches-v7.2.6/` — canonical validated 22-patch series
- `kernel/current-working-tree/` — later development snapshot and debugging material

The current-working-tree directory must not be treated as an additional ordered patch series. It contains follow-up work that was still under investigation.

## Diagnostics

Full diagnostic capture:

https://github.com/nickslkr/orangepi-4a-imx219/blob/main/results/diagnostics-2026-10-04.txt

It includes:

- Armbian release metadata
- running kernel and source state
- camera-related kernel config
- media-controller graph
- /dev/video0 state
- supported V4L2 capture formats
- repeated 0x0000 bridge-format kernel messages

## Scope requested from Armbian maintainers

This handoff is mainly intended to answer three questions:

1. Which parts of the A523/T527 camera support should be carried as Armbian patches versus submitted directly to upstream Linux/media/DT maintainers?
2. Is there already active work for A523/T527 MIPI CSI-2 / CSI capture that this series should be rebased onto?
3. After Linux/Armbian 7.3 verification, what patch split and target subsystem would Armbian prefer for a PR?

The repository contains the reproducible implementation and test material needed for review.