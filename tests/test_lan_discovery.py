"""Hardware-free tests for host/lan_discovery.py: a fake board talks to the
listener over loopback.  Run:  python3 -m unittest discover tests"""
import socket
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recipes" / "lan-discovery" / "host"))
import lan_discovery as ld  # noqa: E402

PORT = 38999  # not the real port, so tests don't interfere with real boards


class FakeBoard:
    def __init__(self, id):
        self.id = id
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(1.0)

    def hello(self, extra=""):
        msg = f"LANDISC1 HELLO id={self.id} mac=AA:BB:CC:DD:EE:FF ip=127.0.0.1 rssi=-50 lib=1.0.0{extra}"
        self.sock.sendto(msg.encode(), ("127.0.0.1", PORT))

    def recv(self):
        try:
            return self.sock.recvfrom(512)[0].decode()
        except socket.timeout:
            return None

    def close(self):
        self.sock.close()


class ListenerTest(unittest.TestCase):
    def setUp(self):
        self.lis = None
        self.board = None

    def tearDown(self):
        if self.lis:
            self.lis.stop()
            self.lis.join(2)
        if self.board:
            self.board.close()

    def start(self, id):
        self.lis = ld.Listener(id, PORT, who=False)
        self.lis.start()

    def test_matching_id_gets_ack_and_is_recorded(self):
        self.start("team07")
        self.board = FakeBoard("team07")
        self.board.hello(" http=80")
        self.assertEqual(self.board.recv(), "LANDISC1 ACK id=team07")
        self.assertTrue(self.lis.found.wait(2))
        dev = self.lis.get()
        self.assertEqual((dev.ip, dev.rssi, dev.fields.get("http")), ("127.0.0.1", -50, "80"))

    def test_other_team_gets_no_ack(self):
        self.start("team07")
        self.board = FakeBoard("team99")
        self.board.hello()
        self.assertIsNone(self.board.recv())
        self.assertFalse(self.lis.found.is_set())
        self.assertIsNotNone(self.lis.get("team99"))  # still listed, just not paired

    def test_scan_mode_never_acks(self):
        self.start(None)
        self.board = FakeBoard("team07")
        self.board.hello()
        self.assertIsNone(self.board.recv())
        time.sleep(0.2)
        self.assertIn("team07", self.lis.devices)

    def test_parse(self):
        self.assertEqual(ld.parse(b"LANDISC1 ACK id=a"), ("ACK", {"id": "a"}))
        self.assertEqual(ld.parse(b"garbage"), (None, {}))
        self.assertEqual(ld.parse(b"ESP32CAM HELLO id=a"), (None, {}))


if __name__ == "__main__":
    unittest.main()
