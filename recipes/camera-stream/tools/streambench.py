#!/usr/bin/env python3
"""Measure ESP32-CAM MJPEG stream: camera-side capture interval (X-Timestamp)
vs. arrival interval on this machine, frame sizes, throughput.

    python3 tools/streambench.py <camera-ip> [seconds]
"""
import http.client, statistics as st, sys, time

if len(sys.argv) < 2:
    sys.exit("usage: python3 streambench.py <camera-ip> [seconds]   (find the IP: lan_discovery.py --id <TEAM_ID>)")
host = sys.argv[1]
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 10

c = http.client.HTTPConnection(host, 81, timeout=5)
t_req = time.time()
c.request("GET", "/stream")
r = c.getresponse()
arr, cam, sizes = [], [], []
first = None
while time.time() - t_req < dur:
    length = ts = None
    while True:
        line = r.readline().strip().lower()
        if line.startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
        elif line.startswith(b"x-timestamp:"):
            sec, usec = line.split(b":", 1)[1].strip().split(b".")
            ts = int(sec) + int(usec) / 1e6
        elif line == b"" and length is not None:
            break
    data = r.read(length)
    now = time.time()
    if first is None:
        first = now - t_req
    arr.append(now); sizes.append(len(data))
    if ts is not None:
        cam.append(ts)
c.close()

def iv(xs):
    d = [(b - a) * 1000 for a, b in zip(xs, xs[1:])]
    return d

a = iv(arr)
span = arr[-1] - arr[0]
print(f"time-to-first-frame  {first*1000:.0f} ms")
print(f"frames               {len(arr)}  in {span:.1f}s  -> received {(len(arr)-1)/span:.1f} fps")
print(f"frame size           avg {st.mean(sizes)/1024:.1f} KB  max {max(sizes)/1024:.1f} KB")
print(f"throughput           {sum(sizes)*8/span/1000:.0f} kbps")
print(f"arrival interval ms  mean {st.mean(a):.1f}  p50 {st.median(a):.1f}  p95 {sorted(a)[int(len(a)*.95)]:.1f}  max {max(a):.1f}  sd {st.pstdev(a):.1f}")
if len(cam) > 2:
    cv = iv(cam)
    cspan = cam[-1] - cam[0]
    print(f"camera interval ms   mean {st.mean(cv):.1f}  p50 {st.median(cv):.1f}  p95 {sorted(cv)[int(len(cv)*.95)]:.1f}  max {max(cv):.1f}  -> camera {(len(cam)-1)/cspan:.1f} fps")
    # drift between camera clock and arrival = queueing delay growth/jitter
    off = [(x - y) * 1000 for x, y in zip(arr, cam)]
    base = min(off)
    q = [o - base for o in off]
    print(f"relative queue delay ms  p50 {st.median(q):.0f}  p95 {sorted(q)[int(len(q)*.95)]:.0f}  max {max(q):.0f}")
