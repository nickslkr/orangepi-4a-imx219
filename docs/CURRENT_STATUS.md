# Orange Pi 4A IMX219 Current Status

Date:
2026-09-30

## Hardware

Board:
Orange Pi 4A

SoC:
Allwinner T527 / A523 family

Sensor:
Sony IMX219

Interface:
MIPI CSI-2

## Kernel

Base:
Linux 7.2.6

Branch:
opi4a-imx219

Patch series:
22 commits

Implemented:

- A523 MIPI CSI-2 receiver
- A523 CSI capture backend
- MIPI D-PHY support
- Orange Pi 4A DT changes
- IMX219 DT integration

## Capture

Working device:

V4L2 RAW10 capture

Format:

1920x1080 RG10

## Userspace pipeline

RAW10
 ->
OpenCL debayer
 ->
YUV420
 ->
v4l2loopback
 ->
uStreamer

OpenCL:

Device:
Mali-G57 Panfrost

Performance:

Capture:
~10 fps output

Processing:
~81 ms/frame

## Webcam integration

Klipper/Moonraker:

cam1:
Logitech USB camera

cam2:
IMX219 virtual camera

IMX219 stream:

/webcam2/?action=stream

## Known limitations

- Pipeline currently optimized for 1080p mode
- Full sensor resolutions require additional tuning
- Final upstream submission preparation required
