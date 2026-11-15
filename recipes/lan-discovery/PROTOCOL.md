# LanDiscovery protocol v1

UDP, port **38271** on both sides. ASCII, one message per datagram, ≤ 512 bytes.
Format: `LANDISC1 <KIND> key=value key=value ...` — tokens separated by single
spaces, keys and values contain no spaces or `=`.

## Messages

| kind | direction | example | meaning |
|---|---|---|---|
| `HELLO` | board → laptop | `LANDISC1 HELLO id=team07 mac=AA:BB:CC:DD:EE:FF ip=192.168.0.42 rssi=-58 lib=1.0.0 http=80` | "I am here". Required keys: `id`, `mac`, `ip`, `rssi`, `lib`. Extra keys come from `addField()`. |
| `ACK` | laptop → board | `LANDISC1 ACK id=team07` | sent to the HELLO's source address/port. The board accepts it only if `id` equals its own id. The board stores the ACK's source IP as `hostIP()`. |
| `WHO` | laptop → broadcast | `LANDISC1 WHO id=team07` or `LANDISC1 WHO id=*` | "announce now". A board whose id matches (or `*`) replies with a unicast HELLO to the sender. |

The laptop takes the board's address from the **UDP source address**, not from
the `ip=` field (`ip=` is informational).

## Board timing

```
not connected      → wait 500 ms
known host         → unicast HELLO every 30 s (setHeartbeat); 3 missed ACKs → forget host
no host:
  1. broadcast HELLO to subnet broadcast and 255.255.255.255, wait 700 ms, twice
  2. unicast HELLO to every .1–.254 of own /24 (8 ms apart, then wait 1.5 s)
     — only on the first two cycles, then at most once per 5 minutes
  3. wait 10 s → 20 s → 40 s → 60 s (cap) answering WHO immediately; repeat
```

WHO from a laptop gets an immediate reply in every state, so a laptop that
starts later still finds a board that is in its long wait.

## Laptop behavior (host/lan_discovery.py)

- Binds UDP 38271 (SO_REUSEADDR, plus SO_REUSEPORT where available).
- With an id: ACKs only HELLOs carrying that id; broadcasts `WHO id=<id>` every
  3 s until that board is heard and again if it is silent for 70 s.
- Without an id: never ACKs; broadcasts `WHO id=*` every 5 s; lists what it hears.

## Testing hook

Build the board with `-DLANDISC_SKIP_BROADCAST` and run the laptop with
`--no-who` to exercise the unicast sweep path alone.
