# esp-photo-service

A port of the `main` branch of the Pi-Zero-Server project to the ESP32-S3 CAM (OV3660 / OV5640): a web panel with a live preview and automatic upload of snapshots to a server.

## What it does
- Every N seconds it captures a frame (FHD, JPEG straight from the sensor) and sends `POST <server_url>?product=<MAC>` (multipart, field `file`). Timeout is 5 s, with one retry after 5 s. A failed send is dropped (no queue, nothing is stored). The camera is released before sending, so the panel stays responsive while the server is slow or down. Frames are not stored anywhere.
- Web panel on port 80: `/` page, `/preview` 640x480 JPEG (rotated 180°), `/auto_start?interval=N`, `/auto_stop`, `/status` -> `{"running":bool,"interval":int}`.
- Colour tuning (applies to the preview and to the frames sent to the server): `-`/`+` buttons in the panel for brightness, contrast, saturation, exposure, white balance and lens correction. Changes apply immediately; the **Save** button stores them in flash. Routes: `/control?var=<brightness|contrast|saturation|ae_level|wb_mode|lenc>&val=N`, `/camera_settings`, `/camera_save`, `/camera_reset`.
- Upload server: the panel has **Server IP** and **Port** fields (default port 8000) and a **Save server** button (disabled while auto capture is running), so the Wi-Fi portal is not required. The upload URL is built as `http://<IP>:<port>/upload` and stored in flash. Routes: `/set_server?ip=<IPv4>&port=<N>` (returns the stored server as JSON). The Wi-Fi portal also asks for the server IP and port.
- Current settings: the **Current settings** block at the bottom of the panel shows what is stored on the board (upload URL, interval, saved colour values) and marks colour changes that are not saved yet. Route: `/settings` -> JSON `{"server":{"ip","port","url"},"interval","running","saved":{...},"live":{...}}`. All panel messages are in English.
- Panel layout: three tabs. **Camera** (preview, auto capture, photo server, colour tuning), **Updates** (firmware version, check/install update, firmware server) and **Diagnostics** (warnings, board health, event log). A status strip with temperature, Wi-Fi signal and auto-capture state stays visible on every tab. Tabs can be opened directly: `/#camera`, `/#updates`, `/#diag`.
- Board health: `/health.json` (old name `/sysinfo` still works) -> JSON with temperature, Wi-Fi signal, IP, uptime, heap/PSRAM, restart reason, photos sent/failed and average rate. Loaded when the page opens, on **Refresh now**, and every 15 s only if **Auto-refresh** is ticked (off by default).
- Event log: `/log` (plain text). Only in RAM (last 50 lines of 128 bytes, see `src/log.h`); nothing is written to flash. A copy of the previous run is kept in RTC memory, so after a crash, watchdog or software restart the **Before last restart** view shows what happened. It is lost on a power cut. Logged: boot and restart reason, Wi-Fi connect/disconnect (with reason code), camera errors, failed uploads, web clients appearing/leaving, temperature above 70 C, OTA and firmware updates. If Wi-Fi is lost for 10 minutes the board restarts itself.
- `product` is the board's eFuse MAC (the equivalent of a Raspberry Pi serial number).

## Build and flash
Secrets first: copy `include/secrets.example.h` to `include/secrets.h` and set `OTA_PASSWORD` (and `OTA_HOSTNAME` if you want a different name). `secrets.h` is git-ignored.

**First time, over USB** (the default, also the Upload button in VS Code):
```
pio run -t upload
pio device monitor
```

**Later, over Wi-Fi from the computer** (ArduinoOTA, board and computer on the same network):
```
pio run -e esp32-s3-cam-ota -t upload
```
The host in `upload_port` (`esp32cam.local`) must match `OTA_HOSTNAME`; if `.local` is not resolved on your PC, put the board IP there. The password is taken from `secrets.h`. USB keeps working at any time.

**Update by button from a firmware server** (Updates tab): every build writes `firmware_out/firmware.bin` and `firmware_out/version.json`. Put these two files on any web server in the network (double-click `start_fw_server.bat` in the project root: a window with Start/Stop buttons shows the address to type on the board and the progress of the board's download; `scripts/fw_server.py --dir firmware_out --port 8080` is the same server without a window and checks that the two files match; plain `python -m http.server 8080` inside `firmware_out` also works), enter its IP and port in the **Firmware server** block, press **Check for update**, then **Install update**. The board downloads the file, compares its SHA-256 with `version.json` before switching to it, and restarts. A bad download keeps the old firmware. There is no automatic rollback of a firmware that is valid but faulty; reflash over USB in that case.
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
