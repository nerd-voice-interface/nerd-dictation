#!/usr/bin/env python3
"""The recognition protocol end to end: the real AsrServer and Recognizer
(asr.py) with a fake backend (no GPU), the real AsrClient over the unix
socket ("local") and over the stdio bridge (scripts/nerd-dictation-asr-bridge,
the program a remote client runs through ssh), in a private XDG_RUNTIME_DIR.

  A  no server: recognize() fails within LINK_WAIT_SECONDS with the
     reason; status says so
  B  two clients at once: both recognized, serialized on the one worker
  C  a client dropping mid-segment: the server notices, skips the work and
     serves the next client
  D  server restarted between two segments: the client reconnects at once
     and the second segment is recognized
  E  protocol bounds: oversized frame, AUDIO before HELLO, unknown version
  F  a server that never answers HELLO: recognize() fails within
     LINK_WAIT_SECONDS (not the 20 s handshake), stop() returns at once
  G  the bridge: a client through the stdio bridge gets the same text

Run: python3 tests/test_asr_protocol.py
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import types

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, REPO)
RT = tempfile.mkdtemp()
os.chmod(RT, 0o700)
os.environ["XDG_RUNTIME_DIR"] = RT

from nerd_dictation_ex import asr  # noqa: E402

FAILURES = 0


def check(name, ok, detail=""):
    global FAILURES
    print(f"{'PASS' if ok else 'FAIL'}: {name}" + (f"  [{detail}]" if detail else ""), flush=True)
    FAILURES += not ok


class FakeBackend:
    """Text = "<seconds of audio>s"; DELAY seconds per segment."""
    supports_streaming = False
    sample_rate = asr.SAMPLE_RATE
    DELAY = 0.0

    def __init__(self):
        self.buf = b""
        self.recognized = 0

    def feed(self, data):
        self.buf += data

    def finalize(self):
        time.sleep(self.DELAY)
        self.recognized += 1
        text, self.buf = f"{len(self.buf) / 32000:.2f}s", b""
        return types.SimpleNamespace(text=text, is_final=True, confidence=1.0)

    def reset(self):
        self.buf = b""


class Server:
    def __init__(self, delay=0.0):
        self.backend = FakeBackend()
        self.backend.DELAY = delay
        self.recognizer = asr.Recognizer(self.backend, "fake")
        self.recognizer.start()
        self.server = asr.AsrServer(self.recognizer)
        self.server.start()

    def stop(self):
        self.server.stop()
        self.recognizer.stop()


def audio(seconds):
    return b"\0\0" * int(asr.SAMPLE_RATE * seconds)


def connected(client, timeout=10.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if client.status()[0]:
            return True
        time.sleep(0.05)
    return False


def capture_stderr():
    """Collect what the server prints (its log) during a scenario."""
    r, w = os.pipe()
    saved = os.dup(2)
    os.dup2(w, 2)
    os.close(w)
    sys.stderr = os.fdopen(os.dup(2), "w", buffering=1)
    chunks = []
    t = threading.Thread(target=lambda: chunks.append(os.read(r, 1 << 20)) or None, daemon=True)

    def done():
        sys.stderr.flush()
        os.dup2(saved, 2)
        sys.stderr = sys.__stderr__
        os.set_blocking(r, False)
        out = b""
        try:
            while True:
                part = os.read(r, 65536)
                if not part:
                    break
                out += part
        except BlockingIOError:
            pass
        return out.decode(errors="replace")
    return done


# A. no server
c = asr.AsrClient("local")
c.start()
time.sleep(0.5)
t = time.monotonic()
try:
    c.recognize(audio(5))
    err = None
except RuntimeError as e:
    err = str(e)
ms = (time.monotonic() - t) * 1000
check("A no server -> error, not text", err is not None, err or "")
check("A no server -> the reason names the socket", err is not None and "nerd-dictation-asr.sock" in err)
check("A no server -> answer within LINK_WAIT_SECONDS", ms < asr.LINK_WAIT_SECONDS * 1000 + 500,
      f"{ms:.0f} ms")
check("A no server -> status says not connected", c.status()[0] is False)
c.stop()

# B. two clients at once, 1 s per segment: both recognized, serialized
srv = Server(delay=1.0)
c1, c2 = asr.AsrClient("local"), asr.AsrClient("local")
c1.start(); c2.start()
check("B both clients connected", connected(c1) and connected(c2))
res = {}


def ask(name, client):
    t = time.monotonic()
    res[name] = (client.recognize(audio(5)), time.monotonic() - t)


ths = [threading.Thread(target=ask, args=(n, c)) for n, c in (("one", c1), ("two", c2))]
[th.start() for th in ths]
[th.join() for th in ths]
texts = sorted(v[0] for v in res.values())
waits = sorted(v[1] for v in res.values())
check("B busy -> both recognized", texts == ["5.00s", "5.00s"], str(texts))
check("B busy -> serialized (one ~1 s, the other ~2 s)",
      waits[0] < 1.7 and waits[1] > 1.8, f"{waits[0]:.2f}/{waits[1]:.2f} s")
c1.stop(); c2.stop()
srv.stop()

# C. a client drops mid-segment (raw socket: sends AUDIO, then closes)
log_done = capture_stderr()
srv = Server(delay=1.5)
s = socket.socket(socket.AF_UNIX)
s.connect(str(asr.socket_path()))
asr.send_frame(s, asr.HELLO, struct.pack("<H", asr.PROTOCOL_VERSION))
asr.recv_frame(s, idle_timeout=5)
blocker = asr.AsrClient("local")
blocker.start()
connected(blocker)
th = threading.Thread(target=lambda: blocker.recognize(audio(2)))
th.start()                         # occupies the worker for 1.5 s
time.sleep(0.2)
asr.send_frame(s, asr.AUDIO, audio(5))
time.sleep(0.2)
s.close()                          # gone while its segment waits in the queue
th.join()
time.sleep(2.0)
c = asr.AsrClient("local")
c.start()
connected(c)
try:
    text = c.recognize(audio(5))
except RuntimeError as e:
    text = f"<{e}>"
c.stop(); blocker.stop()
recognized = srv.backend.recognized
srv.stop()
log = log_done()
check("C drop -> the server noticed", "client left" in log or "оборвался" in log, "")
check("C drop -> the gone client's segment was not recognized", recognized == 2,
      f"backend recognized {recognized} (blocker + next client)")
check("C drop -> next client still served", text == "5.00s", text)

# D. server restarted between two segments: the client is back by itself
#    (local: within a second), and a link the server dropped between two
#    segments costs one immediate reconnect, not the segment
srv = Server()
c = asr.AsrClient("local")
c.start()
connected(c)
first = c.recognize(audio(5))
srv.stop()
down = c.status()
time.sleep(1.2)
down = c.status()
srv = Server()
t = time.monotonic()
back = connected(c, timeout=5)
back_s = time.monotonic() - t
second = c.recognize(audio(3)) if back else "<not reconnected>"
check("D restart -> status says not connected while the server is down", down[0] is False,
      down[1])
check("D restart -> reconnected by itself within 2 s", back and back_s < 2.0, f"{back_s:.2f} s")
check("D restart -> next segment recognized", (first, second) == ("5.00s", "3.00s"),
      f"{first} / {second}")
with srv.server._clients_lock:        # the server drops the link between segments
    for conn in list(srv.server._conns):
        conn.shutdown(socket.SHUT_RDWR)
try:
    third = c.recognize(audio(2))
except RuntimeError as e:
    third = f"<{e}>"
check("D dropped link -> one immediate reconnect, segment recognized", third == "2.00s", third)
c.stop()

# E. protocol bounds
def raw(data):
    s = socket.socket(socket.AF_UNIX)
    s.connect(str(asr.socket_path()))
    s.settimeout(5)
    s.sendall(data)
    try:
        return s.recv(200)
    except socket.timeout:
        return b"TIMEOUT"
    finally:
        s.close()


check("E oversized frame refused", b"out of range" in raw(struct.pack("<IB", 50_000_000, 1)))
check("E AUDIO before HELLO refused", b"expected HELLO" in raw(struct.pack("<IB", 3, 3) + b"xy"))
check("E unknown protocol version refused", b"not supported" in raw(struct.pack("<IBH", 3, 1, 99)))
srv.stop()

# F. a server that accepts and never answers HELLO
path = str(asr.socket_path())
silent = socket.socket(socket.AF_UNIX)
silent.bind(path)
silent.listen(4)
held = []
threading.Thread(target=lambda: [held.append(silent.accept()) for _ in range(4)],
                 daemon=True).start()
c = asr.AsrClient("local")
c.start()
time.sleep(1.0)                      # the keeper is inside the handshake
t = time.monotonic()
try:
    c.recognize(audio(1))
    err = None
except RuntimeError as e:
    err = str(e)
ms = (time.monotonic() - t) * 1000
check("F stuck handshake -> recognize fails within LINK_WAIT_SECONDS",
      err is not None and ms < asr.LINK_WAIT_SECONDS * 1000 + 500,
      f"{ms:.0f} ms: {err}")
t = time.monotonic()
c.stop()
ms = (time.monotonic() - t) * 1000
check("F stuck handshake -> stop returns at once", ms < 3000, f"{ms:.0f} ms")
silent.close()
os.unlink(path)

# G. through the stdio bridge, as a remote client runs it over ssh
srv = Server()
s = socket.socket(socket.AF_UNIX)
s.connect(path)
s.close()
ours, theirs = socket.socketpair()
bridge = subprocess.Popen([sys.executable, str(asr.BRIDGE)], stdin=theirs, stdout=theirs)
theirs.close()
link = asr._Link(ours, bridge)
asr.send_frame(ours, asr.HELLO, struct.pack("<H", asr.PROTOCOL_VERSION))
ftype, payload = asr.recv_frame(ours, idle_timeout=5)
asr.send_frame(ours, asr.AUDIO, audio(4))
ftype2, payload2 = asr.recv_frame(ours, idle_timeout=5)
link.close()
check("G bridge -> INFO with the backend name", ftype == asr.INFO and payload[7:] == b"fake")
check("G bridge -> TEXT of the segment", (ftype2, payload2) == (asr.TEXT, b"4.00s"),
      f"{ftype2} {payload2!r}")
srv.stop()

print("ALL PASS" if not FAILURES else f"{FAILURES} FAILED")
sys.exit(1 if FAILURES else 0)
