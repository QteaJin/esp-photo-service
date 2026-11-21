# Esp32-video-server

A port of the `main` branch of the Pi-Zero-Server project to the ESP32-S3 CAM (OV5640): a web panel with a live preview and automatic upload of snapshots to a server.

## What it does
- Every N seconds it captures a frame (FHD, JPEG straight from the sensor) and sends `POST <server_url>?product=<MAC>` (multipart, field `file`). Timeout is 5 s, with one retry after 2 s. Frames are not stored anywhere.
- Web panel on port 80: `/` page, `/preview` 640x480 JPEG (rotated 180°), `/auto_start?interval=N`, `/auto_stop`, `/status` -> `{"running":bool,"interval":int}`.
- `product` is the board's eFuse MAC (the equivalent of a Raspberry Pi serial number).

## Build and flash
```
pio run -t upload
pio device monitor
```
The default board is N16R8 (16 MB flash, 8 MB OPI PSRAM). For a different board, adjust `platformio.ini`
(`memory_type`, `flash_size`, `partitions`) and the camera pinout in `include/pins.h` (Freenove ESP32-S3 CAM by default).

## First run
1. The board starts an access point named `ESP32-Cam-Setup`.
2. Connect to it, open the portal (usually 192.168.4.1), choose your Wi-Fi network and change the server URL if needed.
3. After connecting, the IP address is printed in the serial monitor.

`server_url` and `interval` are stored in NVS. Auto capture does not start by itself after a reboot (same as in `main`).

## Differences from the Pi version
- No cv2/numpy color correction (the sensor's auto white balance is used).
- The panel has no Bootstrap CDN dependency; HTML/JS are embedded in the firmware (`src/web_assets.h`).
- File name timestamps come from SNTP in UTC (`configTzTime` in `src/main.cpp`); until the clock is synced the name is `uptime_<ms>.jpg`.
