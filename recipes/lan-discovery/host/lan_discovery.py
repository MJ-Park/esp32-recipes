#!/usr/bin/env python3
"""lan_discovery — find ESP32 boards running the LanDiscovery recipe.

Part of esp32-recipes (https://github.com/MJ-Park/esp32-recipes), MIT License.
Python 3.8+, standard library only. Protocol: UDP 38271, see PROTOCOL.md.

CLI
    python3 lan_discovery.py --id team07           # wait for team07, print its IP, exit
    python3 lan_discovery.py --id team07 --watch   # keep running, print every change
    python3 lan_discovery.py                       # list all boards on the LAN (no ACK)

Library
    from lan_discovery import find, Listener
    dev = find("team07", timeout=15)       # -> Device or None
    print(dev.ip, dev.fields.get("http"))

    lis = Listener("team07", on_device=lambda d, changed: print(d.ip))
    lis.start()                            # background thread; lis.stop() to end

Only a listener started WITH an id answers the board (ACK). Without an id it
just watches, so it never steals another team's board.
"""
import argparse
import json
import socket
import sys
import threading
import time
from dataclasses import asdict, dataclass, field
from typing import Callable, Dict, List, Optional

PORT = 38271
MAGIC = "LANDISC1"


@dataclass
class Device:
    id: str
    ip: str
    mac: str = ""
    rssi: Optional[int] = None
    fields: Dict[str, str] = field(default_factory=dict)
    first_seen: float = 0.0
    last_seen: float = 0.0


def parse(data: bytes):
    """Return (kind, {key: value}) for a LANDISC1 message, or (None, {})."""
    parts = data.decode("ascii", "replace").split()
    if len(parts) < 2 or parts[0] != MAGIC:
        return None, {}
    kv = dict(p.split("=", 1) for p in parts[2:] if "=" in p)
    return parts[1], kv


def broadcast_targets() -> List[str]:
    targets = ["255.255.255.255"]
    try:  # subnet broadcast of the interface that routes to the LAN (assumes /24)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]
        s.close()
        if not ip.startswith("127."):
            targets.append(ip.rsplit(".", 1)[0] + ".255")
    except OSError:
        pass
    return targets


class Listener(threading.Thread):
    """Background UDP listener. on_device(device, changed) runs on this thread;
    `changed` is True when the board is new or its IP changed."""

    def __init__(self, id: Optional[str] = None, port: int = PORT,
                 on_device: Optional[Callable[[Device, bool], None]] = None,
                 who: bool = True):
        super().__init__(daemon=True)
        self.id, self.port, self.on_device, self.who_enabled = id, port, on_device, who
        self.devices: Dict[str, Device] = {}
        self.lock = threading.Lock()
        self.found = threading.Event()
        self._halt = threading.Event()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):  # let a CLI and a dashboard share the port
            try:
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            except OSError:
                pass
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        try:
            self.sock.bind(("0.0.0.0", port))
        except OSError as e:
            self.sock.close()
            raise OSError(f"UDP port {port} is busy: another listener (e.g. a dashboard) is already "
                          f"running on this laptop. Stop it or use that one. ({e})") from None
        self.sock.settimeout(0.5)

    def who(self):
        """Ask boards to announce now (only boards whose id matches, or all)."""
        msg = f"{MAGIC} WHO id={self.id or '*'}".encode()
        for t in broadcast_targets():
            try:
                self.sock.sendto(msg, (t, self.port))
            except OSError:
                pass

    def get(self, id: Optional[str] = None) -> Optional[Device]:
        with self.lock:
            return self.devices.get(id or self.id)

    def stop(self):
        self._halt.set()

    def run(self):
        last_who = 0.0
        try:
            while not self._halt.is_set():
                now = time.time()
                target = self.get() if self.id else None
                stale = target is None or now - target.last_seen > 70
                if self.who_enabled and stale and now - last_who > (3 if self.id else 5):
                    self.who()
                    last_who = now
                try:
                    data, (addr, port) = self.sock.recvfrom(1024)
                except socket.timeout:
                    continue
                except OSError:
                    if self._halt.is_set():
                        break
                    time.sleep(0.5)
                    continue
                kind, kv = parse(data)
                if kind != "HELLO" or "id" not in kv:
                    continue
                if self.id and kv["id"] == self.id:
                    self.sock.sendto(f"{MAGIC} ACK id={self.id}".encode(), (addr, port))
                self._record(addr, kv)
        finally:
            self.sock.close()

    def _record(self, addr: str, kv: Dict[str, str]):
        now = time.time()
        rssi = kv.get("rssi", "")
        extra = {k: v for k, v in kv.items() if k not in ("id", "mac", "ip", "rssi")}
        with self.lock:
            dev = self.devices.get(kv["id"])
            changed = dev is None or dev.ip != addr
            if dev is None:
                dev = Device(id=kv["id"], ip=addr, first_seen=now)
                self.devices[dev.id] = dev
            dev.ip, dev.mac, dev.last_seen, dev.fields = addr, kv.get("mac", ""), now, extra
            dev.rssi = int(rssi) if rssi.lstrip("-").isdigit() else None
        if self.id is None or dev.id == self.id:
            self.found.set()
        if self.on_device:
            self.on_device(dev, changed)


def find(id: str, timeout: float = 15, port: int = PORT) -> Optional[Device]:
    """Wait up to `timeout` seconds for board `id`; returns its Device or None."""
    lis = Listener(id, port)
    lis.start()
    try:
        lis.found.wait(timeout)
        return lis.get()
    finally:
        lis.stop()


def scan(timeout: float = 5, port: int = PORT) -> List[Device]:
    """List every LanDiscovery board heard within `timeout` seconds (no ACK sent)."""
    lis = Listener(None, port)
    lis.start()
    time.sleep(timeout)
    lis.stop()
    with lis.lock:
        return sorted(lis.devices.values(), key=lambda d: d.id)


def _fmt(d: Device) -> str:
    extra = " ".join(f"{k}={v}" for k, v in d.fields.items())
    rssi = f"{d.rssi}dBm" if d.rssi is not None else "-"
    return f"{d.id:<20} {d.ip:<15} {rssi:>7}  {d.mac}  {extra}"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Find ESP32 boards running LanDiscovery (UDP 38271).")
    ap.add_argument("--id", help="your team/board id; required to answer (ACK) the board")
    ap.add_argument("--timeout", type=float, default=None, help="seconds to wait (default 30, scan 5)")
    ap.add_argument("--watch", action="store_true", help="keep running and print every change")
    ap.add_argument("--json", action="store_true", help="JSON output")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--no-who", action="store_true", help="never broadcast WHO (testing)")
    a = ap.parse_args(argv)

    if a.watch:
        def show(d: Device, changed: bool):
            if changed:
                print(json.dumps(asdict(d)) if a.json else f"{time.strftime('%H:%M:%S')}  {_fmt(d)}", flush=True)
        lis = Listener(a.id, a.port, on_device=show, who=not a.no_who)
        lis.start()
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            return 0

    if a.id:
        lis = Listener(a.id, a.port, who=not a.no_who)
        lis.start()
        t0 = time.time()
        lis.found.wait(a.timeout or 30)
        dev = lis.get()
        lis.stop()
        if not dev:
            print(f"'{a.id}' not found. Same Wi-Fi? Same id on the board? See TROUBLESHOOTING in README.",
                  file=sys.stderr)
            return 1
        print(json.dumps(asdict(dev)) if a.json else dev.ip)
        print(f"  found in {time.time() - t0:.1f}s  {_fmt(dev)}", file=sys.stderr)
        return 0

    devs = scan(a.timeout or 5, a.port)
    if a.json:
        print(json.dumps([asdict(d) for d in devs]))
    else:
        print("\n".join(_fmt(d) for d in devs) or "no boards heard")
    return 0 if devs else 1


if __name__ == "__main__":
    sys.exit(main())
