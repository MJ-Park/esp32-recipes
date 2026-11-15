#!/usr/bin/env python3
"""ESP32-CAM dashboard (localhost). Part of esp32-recipes, MIT License.

Finds the camera by team id (LanDiscovery, UDP 38271), keeps ONE upstream MJPEG
connection to it (the ESP32 handles only a couple of stream clients) and fans
frames out to any number of browser tabs. Proxies /status and /control.

    python3 server.py --id team00-cam            # then open http://localhost:8000
    python3 server.py --host 192.168.0.50        # skip discovery

Python 3.8+, standard library only.
"""
import argparse
import http.client
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lan_discovery import Device, Listener  # noqa: E402

ROOT = Path(__file__).resolve().parent / "static"
STREAM_PORT = 81
BOUNDARY = "frame"


class Camera:
    """Where the camera is and how we learned it."""

    def __init__(self):
        self.host = None
        self.via = None
        self.dev = None  # last Device seen via discovery
        self.on_change = None

    def set(self, host, via, dev=None):
        changed = host != self.host
        self.host, self.via = host, via
        if dev is not None:
            self.dev = dev
        if changed:
            print(f"[camera] {host} ({via})", flush=True)
            if self.on_change:
                self.on_change()

    def info(self):
        d = self.dev
        return {
            "cam_host": self.host,
            "cam_id": d.id if d else None,
            "discovered_via": self.via,
            "cam_rssi": d.rssi if d else None,
            "last_hello_age_s": int(time.time() - d.last_seen) if d else None,
        }


cam = Camera()


def jpeg_size(buf: bytes):
    """(width, height) from a JPEG's SOF marker, or None."""
    i, n = 2, len(buf)
    while i + 9 < n:
        if buf[i] != 0xFF:
            i += 1
            continue
        marker = buf[i + 1]
        if marker in (0xC0, 0xC1, 0xC2):
            h, w = struct.unpack(">HH", buf[i + 5:i + 9])
            return w, h
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        i += 2 + struct.unpack(">H", buf[i + 2:i + 4])[0]
    return None


class Grabber(threading.Thread):
    """Reads the camera's MJPEG stream and keeps the latest frame + stats."""

    def __init__(self):
        super().__init__(daemon=True)
        self.cond = threading.Condition()
        self.frame, self.seq, self.size, self.last_ts = None, 0, None, 0.0
        self.connected, self.error, self.reconnects = False, None, 0
        self._times = []  # (ts, nbytes) of recent frames
        self.conn = None
        self.wake = threading.Event()

    def host_changed(self):
        """Drop the current upstream so the next connect uses the new address."""
        conn = self.conn
        if conn is not None and conn.sock is not None:
            try:
                conn.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self.wake.set()

    def stats(self):
        now = time.time()
        with self.cond:
            recent = [t for t in self._times if now - t[0] <= 3.0]
            fps = kbps = 0.0
            if len(recent) >= 2:
                span = recent[-1][0] - recent[0][0]
                if span > 0:
                    fps = (len(recent) - 1) / span
                    kbps = sum(b for _, b in recent[1:]) * 8 / 1000 / span
            return {
                **cam.info(),
                "connected": self.connected and (now - self.last_ts) < 3.0,
                "fps": round(fps, 1),
                "kbps": round(kbps),
                "frame_bytes": len(self.frame) if self.frame else 0,
                "width": self.size[0] if self.size else None,
                "height": self.size[1] if self.size else None,
                "frames": self.seq,
                "last_frame_age_ms": int((now - self.last_ts) * 1000) if self.last_ts else None,
                "reconnects": self.reconnects,
                "error": self.error,
            }

    def run(self):
        backoff = 1.0
        while True:
            if not cam.host:
                self.error = "카메라 찾는 중 (UDP 38271)…"
                self.wake.wait(1.0)
                self.wake.clear()
                continue
            try:
                self._read_stream()
                backoff = 1.0
            except Exception as e:  # noqa: BLE001 - keep retrying whatever happens
                self.error = f"{type(e).__name__}: {e}"
            with self.cond:
                self.connected = False
            self.reconnects += 1
            if self.wake.wait(backoff):
                self.wake.clear()
                backoff = 1.0
            else:
                backoff = min(backoff * 2, 8.0)

    def _read_stream(self):
        conn = http.client.HTTPConnection(cam.host, STREAM_PORT, timeout=5)
        self.conn = conn
        conn.request("GET", "/stream")
        resp = conn.getresponse()
        if resp.status != 200:
            raise RuntimeError(f"stream HTTP {resp.status}")
        with self.cond:
            self.connected = True
        self.error = None
        try:
            while True:
                length = None
                while True:  # part headers until the blank line
                    line = resp.readline()  # readline/read decode chunked transfer if used
                    if not line:
                        raise ConnectionError("stream closed")
                    s = line.strip().lower()
                    if s.startswith(b"content-length:"):
                        length = int(s.split(b":", 1)[1])
                    elif s == b"" and length is not None:
                        break
                data = resp.read(length)
                if len(data) != length:
                    raise ConnectionError("short frame")
                now = time.time()
                with self.cond:
                    self.frame, self.last_ts = data, now
                    self.seq += 1
                    if self.seq % 15 == 1 or self.size is None:
                        self.size = jpeg_size(data) or self.size
                    self._times.append((now, len(data)))
                    del self._times[:-120]
                    self.cond.notify_all()
        finally:
            self.conn = None
            conn.close()


grabber = Grabber()


def cam_get(path: str, timeout=4.0):
    if not cam.host:
        raise RuntimeError("camera not found yet")
    t0 = time.time()
    with urllib.request.urlopen(f"http://{cam.host}{path}", timeout=timeout) as r:
        body = r.read()
    return body, (time.time() - t0) * 1000


class Handler(BaseHTTPRequestHandler):
    server_version = "esp32-recipes-camera/1.0"

    def log_message(self, fmt, *args):
        if args and "/api/" in str(args[0]):
            return
        super().log_message(fmt, *args)

    def _send(self, code, body: bytes, ctype="application/json", extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj).encode())

    def do_GET(self):
        u = urlparse(self.path)
        p = u.path
        try:
            if p in ("/", "/index.html"):
                return self._send(200, (ROOT / "index.html").read_bytes(), "text/html; charset=utf-8")
            if p == "/stream":
                return self._stream()
            if p == "/api/stats":
                return self._json(grabber.stats())
            if p == "/api/status":
                body, ms = cam_get("/status")
                return self._json({"ok": True, "latency_ms": round(ms), "status": json.loads(body)})
            if p == "/api/control":
                q = {k: v[0] for k, v in parse_qs(u.query).items()}
                if "var" not in q or "val" not in q:
                    return self._json({"ok": False, "error": "var and val required"}, 400)
                _, ms = cam_get("/control?" + urlencode({"var": q["var"], "val": q["val"]}))
                return self._json({"ok": True, "latency_ms": round(ms)})
            if p == "/api/snapshot":
                with grabber.cond:
                    frame = grabber.frame
                if frame is None:
                    frame, _ = cam_get("/capture")
                name = time.strftime("esp32cam-%Y%m%d-%H%M%S.jpg")
                return self._send(200, frame, "image/jpeg",
                                  {"Content-Disposition": f'attachment; filename="{name}"'})
            return self._json({"error": "not found"}, 404)
        except (BrokenPipeError, ConnectionResetError):
            return
        except Exception as e:  # noqa: BLE001
            try:
                return self._json({"ok": False, "error": f"{type(e).__name__}: {e}"}, 502)
            except Exception:  # noqa: BLE001
                return

    def _stream(self):
        self.send_response(200)
        self.send_header("Content-Type", f"multipart/x-mixed-replace; boundary={BOUNDARY}")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        last = -1
        try:
            while True:
                with grabber.cond:
                    grabber.cond.wait_for(lambda: grabber.seq != last, timeout=5)
                    if grabber.seq == last or grabber.frame is None:
                        continue
                    frame, last = grabber.frame, grabber.seq
                self.wfile.write(f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\n"
                                 f"Content-Length: {len(frame)}\r\n\r\n".encode() + frame + b"\r\n")
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            return


def main():
    ap = argparse.ArgumentParser(description="ESP32-CAM dashboard")
    ap.add_argument("--id", default=os.environ.get("CAM_ID"), help="TEAM_ID set in CameraStream.ino")
    ap.add_argument("--host", default=os.environ.get("CAM_HOST"), help="camera IP (skips discovery)")
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8000")))
    a = ap.parse_args()
    if not a.id and not a.host:
        ap.error("give --id <TEAM_ID> (same as in CameraStream.ino) or --host <ip>")

    cam.on_change = grabber.host_changed
    if a.host:
        cam.set(a.host, "manual")
    else:
        def on_device(dev: Device, changed: bool):
            if dev.id == a.id:
                cam.set(dev.ip, "udp", dev)
        Listener(a.id, on_device=on_device).start()

    grabber.start()
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    srv.daemon_threads = True
    print(f"Dashboard → http://localhost:{a.port}  "
          f"({'camera ' + a.host if a.host else 'looking for id=' + a.id + ' on UDP 38271'})", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
