# lan-discovery — agent instructions

Goal: a laptop finds the board's IP by `TEAM_ID` over UDP 38271. No static IP, no mDNS.
Board: any ESP32, Arduino core 3.x. Laptop: Python 3.8+, stdlib only.

## Files

| file | role |
|---|---|
| `src/LanDiscovery.h`, `src/LanDiscovery.cpp` | board side — copy both |
| `host/lan_discovery.py` | laptop side — CLI and importable module |
| `examples/Basic/Basic.ino` | complete minimal sketch (Wi-Fi + discovery) |
| `PROTOCOL.md` | wire format — only needed to port to another language |

## Add to an existing sketch

1. Copy `src/LanDiscovery.h` and `src/LanDiscovery.cpp` into the sketch folder
   (next to the `.ino`) with `cp`, or without a local clone:
   ```bash
   for f in LanDiscovery.h LanDiscovery.cpp; do
     curl -fLO https://raw.githubusercontent.com/MJ-Park/esp32-recipes/main/recipes/lan-discovery/src/$f
   done
   ```
   PlatformIO: put them in `lib/LanDiscovery/` instead. Don't read or retype them.
2. Top of the sketch:
   ```cpp
   #include "LanDiscovery.h"
   LanDiscovery disc("TEAM_ID");
   ```
3. In `setup()`, after Wi-Fi connects:
   ```cpp
   disc.addField("http", 80);  // optional key=value (no spaces), call before begin()
   disc.begin();               // background task; also fine before Wi-Fi connects
   ```
4. No Wi-Fi code in the sketch yet? Start from `examples/Basic/Basic.ino`.

Board API (optional): `disc.hasHost()`, `disc.hostIP()` (the laptop's IP — use it
to send data back), `disc.onHost(cb)` (runs on the discovery task; keep it short),
`disc.setHeartbeat(ms)`, `disc.setLog(false)`.

## Laptop side

```bash
python3 lan_discovery.py --id TEAM_ID        # prints the board IP, exit code 0
python3 lan_discovery.py --id TEAM_ID --watch  # keep running, print IP changes
python3 lan_discovery.py                     # list every board (read-only, no ACK)
```
```python
from lan_discovery import find, Listener
dev = find("TEAM_ID", timeout=15)  # Device(id, ip, mac, rssi, fields) or None
```
Only one listener per laptop on UDP 38271 (the camera-stream dashboard includes one).

## Verify

1. `arduino-cli compile --fqbn <fqbn> <sketch-folder>`
2. Upload; then `python3 lan_discovery.py --id TEAM_ID` prints an IP within ~5 s of boot.
3. Serial monitor (115200): `[LanDiscovery] host <laptop-ip> (id=TEAM_ID)`.

## Troubleshooting (in this order)

1. Board and laptop on the same Wi-Fi? (ESP32 is 2.4 GHz only.)
2. Same `TEAM_ID` on both sides, same case?
3. `UDP port 38271 is busy` → another listener/dashboard is running on the laptop.
4. Windows: allow Python at the firewall prompt; network profile must be Private.
5. Venue Wi-Fi with client isolation blocks device-to-device traffic → put both on a phone hotspot.
6. Subnet larger than /24: broadcast still works; the fallback sweep covers only the board's /24.

## Don't

- Don't change the message format or port 38271 unless the user asks.
- Don't replace this with mDNS or a static IP; avoiding that is the point of the recipe.
