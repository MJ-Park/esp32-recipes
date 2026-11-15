# camera-stream — agent instructions

Goal: low-latency MJPEG streaming from an **AI Thinker ESP32-CAM** plus a
localhost browser dashboard that finds the camera by `TEAM_ID` (uses the
lan-discovery recipe; copies are already included here).

## Files

| path | role |
|---|---|
| `firmware/CameraStream/` | complete sketch — `CameraStream.ino`, `LanDiscovery.h/.cpp`, `partitions.csv`. Keep the folder together. |
| `dashboard/server.py` | localhost dashboard + stream fan-out + discovery (stdlib only) |
| `dashboard/lan_discovery.py`, `dashboard/static/index.html` | used by server.py |
| `tools/streambench.py` | measure fps / jitter straight from the camera |

## Steps

1. Copy `firmware/CameraStream/` and `dashboard/` into the user's workspace with
   `cp -R` (or `git clone --depth 1` the repo) — don't retype them. Edit only the
   settings block at the top of `CameraStream.ino`: `TEAM_ID` (and
   `WIFI_SSID`/`WIFI_PASS` if the board has no Wi-Fi saved).
2. Build and upload:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32cam CameraStream
   arduino-cli upload  --fqbn esp32:esp32:esp32cam -p <serial-port> CameraStream
   ```
   `partitions.csv` in the sketch folder is picked up automatically (3.75 MB app;
   NVS at 0x9000 is preserved, so saved Wi-Fi survives). Upload needs download
   mode: ESP32-CAM-MB USB base does it automatically; otherwise hold GPIO0 to GND
   and reset.
3. Run the dashboard on the laptop and open http://localhost:8000:
   ```bash
   python3 dashboard/server.py --id TEAM_ID
   ```
   (`--host <ip>` skips discovery.)

## Verify

- Dashboard header shows `온라인`, the frame rate tile shows > 10 fps at 320×240.
- Or: `python3 tools/streambench.py <camera-ip> 10` (stop the dashboard first —
  the camera serves one stream client at a time; the dashboard fans out to many tabs).

## Merging into an existing camera sketch

Keep these four things; they are where the speed comes from:
1. `WiFi.setSleep(false)` after connecting (modem sleep adds up to ~100 ms per packet).
2. `fb_count = 2`, `fb_location = CAMERA_FB_IN_PSRAM`, `grab_mode = CAMERA_GRAB_LATEST`.
3. `stream_handler` + `send_all`: raw socket, `TCP_NODELAY`, one write per frame, no chunked encoding.
4. `xclk_freq_hz = 20000000` (10 MHz measured worse).

Other camera boards: replace the pin map block (see Arduino's
`CameraWebServer/camera_pins.h`) and the FQBN.

## HTTP API (board)

| endpoint | |
|---|---|
| `:80/status` | JSON: camera settings + `id`, `ip`, `rssi`, `psram`, `fb_count`, `host` |
| `:80/control?var=<name>&val=<n>` | CameraWebServer-compatible names (`framesize`, `quality`, `brightness`, `hmirror`, `vflip`, `led_intensity`, …) |
| `:80/capture` | single JPEG |
| `:81/stream` | multipart MJPEG, one client at a time |

`framesize` values (core 3.x): 5=240×240, 6=QVGA 320×240, 8=CIF 400×296, 10=VGA 640×480, 11=SVGA, 12=XGA, 13=HD, 15=UXGA.

## Troubleshooting

- `Camera init failed` → ribbon cable seated? board really AI Thinker? 5 V ≥ 2 A supply.
- Resets / brownout under load → power supply; avoid long thin USB cables.
- Low or bursty fps → Wi-Fi signal/channel congestion; try a smaller framesize; move closer to the AP.
- Dashboard can't find the camera → follow `recipes/lan-discovery/AGENTS.md` troubleshooting.
