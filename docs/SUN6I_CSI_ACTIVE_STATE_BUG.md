# sun6i-csi active-state bug: SOURCE pad can remain 0x0000

Date: 2026-10-04

## Summary

During Orange Pi 4A / T527 / IMX219 testing, the camera media graph can be created correctly but cold-boot capture startup may fail because the sun6i-csi bridge SOURCE pad active format is not initialized together with the SINK pad.

Observed kernel message:

    sun6i-csi 5800800.csi: unsupported bridge format 0x0000

This can repeat while a camera service retries capture startup.

## Why this matters

The sun6i-csi bridge does not transcode media-bus formats. Its SOURCE pad must therefore represent the same format as its SINK pad.

The relevant init_state logic in the tested 7.2.x code initializes only the SINK pad. If the SOURCE pad is queried/validated before a later set_fmt operation propagates a valid format, its code may remain zero.

That zero media-bus code reaches the bridge/capture format lookup and is rejected.

## Evidence from the test system

Test snapshot:

- Orange Pi 4A
- Linux 7.2.6+ development kernel
- Armbian 26.11.0-trunk.62
- IMX219
- media driver: sun6i-csi
- capture node: /dev/video0

The diagnostic log contains repeated lines such as:

    sun6i-csi 5800800.csi: unsupported bridge format 0x0000

After the media state has been configured, the same system can show the expected graph:

    sun6i-csi-bridge pad0 SINK:
      SRGGB10_1X10/1920x1080

    sun6i-csi-bridge pad1 SOURCE:
      SRGGB10_1X10/1920x1080

and /dev/video0 reports:

    Pixel Format: RG10
    Width/Height: 1920/1080

This is why the issue is treated as an initialization/state problem rather than a missing format capability.

## Local follow-up logic

The local development tree includes propagation in set_fmt equivalent to:

    sink_format = v4l2_subdev_state_get_format(
        state, SUN6I_CSI_BRIDGE_PAD_SINK);

    ...

    src_format = v4l2_subdev_state_get_format(
        state, SUN6I_CSI_BRIDGE_PAD_SOURCE);
    *src_format = *sink_format;

That is correct for set_fmt, but a complete fix also needs init_state to establish a valid SOURCE pad state immediately.

Conceptually, init_state should initialize the SINK default and then copy it to SOURCE, because source cannot differ from sink for this bridge.

## Version status

The hardware was validated on Linux 7.2.6 plus the project camera changes.

Linux 7.2.9 was inspected for this issue; the relevant state initialization is still not sufficient to make a simple version bump the fix.

The project plan is therefore to wait for the Linux/Armbian 7.3 generation, re-check the active-state conversion there, and then validate cold boot before preparing a final submission.

## Relationship to the 22-patch camera series

This is a follow-up bug discovered after the main camera-enablement series was working.

The canonical 22-patch series is preserved separately:

https://github.com/nickslkr/orangepi-4a-imx219/tree/main/kernel/patches-v7.2.6

Do not silently fold this follow-up into that already validated series without re-running patch-series validation and cold-boot tests.

## Diagnostic record

https://github.com/nickslkr/orangepi-4a-imx219/blob/main/results/diagnostics-2026-10-04.txt
