# AGENTS.md — esp32-recipes

Proven, hardware-tested ESP32 (Arduino) building blocks. When the user names a
recipe, **reuse these files — do not re-implement the feature.**

## Recipe index

| recipe | use when the user wants… | read next |
|---|---|---|
| `lan-discovery` | to find an ESP32's IP from a laptop, "no IP setup", auto-discovery, laptop ↔ board pairing by team id | `recipes/lan-discovery/AGENTS.md` |
| `camera-stream` | ESP32-CAM live video, higher FPS / lower latency, a browser dashboard, camera settings | `recipes/camera-stream/AGENTS.md` |

## Rules

1. Read only the AGENTS.md of the recipe(s) you need. Don't scan the whole repo.
2. Copy files with a shell command (`cp`, or `curl -fLO` from
   `https://raw.githubusercontent.com/MJ-Park/esp32-recipes/main/<path>`) —
   never retype file contents; that wastes tokens and introduces diffs.
   Change only the marked settings (`TEAM_ID`, `WIFI_SSID`, `WIFI_PASS`, pin
   maps). Keep the UDP protocol and port unchanged so every tool in this repo
   stays compatible.
3. `TEAM_ID`: if the user didn't give one, ask. It must be unique at the event;
   letters, digits, `-`, `_`; max 32 chars; case-sensitive.
4. Wi-Fi credentials go only into the user's own sketch. Never commit or push
   them. If the board already has Wi-Fi saved, leave `WIFI_SSID` empty
   (`WiFi.begin()` reuses the saved network).
5. Toolchain: Arduino-ESP32 core **3.x** (`arduino-cli core install esp32:esp32`).
   FQBN: generic `esp32:esp32:esp32`, AI Thinker ESP32-CAM `esp32:esp32:esp32cam`.
   PlatformIO: put recipe `src/` files in `lib/<Name>/`.
6. Verify: compile; if a board is connected, upload and run the recipe's
   "Verify" step. Tell the user plainly what you could not verify.
7. Opening the serial port resets most ESP32 boards (DTR/RTS). That is expected.
8. Laptop side is Python 3.8+ standard library only — don't add dependencies.
