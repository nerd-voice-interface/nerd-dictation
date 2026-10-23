"""
Remote recognition server: other machines' nerd-dictation (backend "remote",
see ~/forge/nerd-dictation-remote) send speech segments here and get the
text back. Recognition runs on this daemon's own loaded backend, in its one
recognizer worker, so remote segments and the local microphone's segments
share the model in arrival order; there is never a second model on the GPU.

Transport is the client's ssh connection: the client runs a small stdio
bridge on this host over ssh (nerd-dictation-remote/src/bridge.py), which
connects to socket_path(). The socket is
in the user's XDG_RUNTIME_DIR (mode 0700) and is itself 0600, so only this
user - locally or through ssh with this user's keys - can reach it.

Protocol (version 1), little-endian frames:
    u32 length (bytes after this field) | u8 type | payload
    HELLO  c->s  u16 version
    INFO   s->c  u16 version, u32 sample rate (16000), u8 channels (1),
                 then the backend name in UTF-8
    AUDIO  c->s  int16 PCM, 16 kHz mono, one whole segment, <= 30 s
    TEXT   s->c  the recognized text, UTF-8 (may be empty)
    ERROR  s->c  a message, UTF-8; the connection stays usable unless the
                 error is about the protocol itself
One segment is in flight per connection: AUDIO, then TEXT or ERROR.
"""

import os
import select
import socket
import struct
import sys
import threading
import time
from pathlib import Path

PROTOCOL_VERSION = 1
SAMPLE_RATE = 16000
CHANNELS = 1

HELLO, INFO, AUDIO, TEXT, ERROR = 1, 2, 3, 4, 5
MAX_SEGMENT_SECONDS = 30
MAX_FRAME = 1 + MAX_SEGMENT_SECONDS * SAMPLE_RATE * 2
MAX_CONNECTIONS = 8
# Remote segments allowed in the recognizer queue at once, over all clients:
# bounds the memory (x 30 s of audio) and how long a local segment can wait
# behind remote ones (each 30 s segment took ~0.7 s on corentin's GPU).
MAX_QUEUED = MAX_CONNECTIONS
# A client that started a frame must finish it in this time; between frames
# it may idle for as long as its ssh connection lives.
FRAME_SECONDS = 30
# How long a segment may wait for the recognizer worker before the client is
# told the server is busy.
RECOGNIZE_SECONDS = 60

_HEADER = struct.Struct("<IB")


def socket_path() -> Path:
    runtime = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return Path(runtime) / "nerd-dictation-asr.sock"


class ProtocolError(Exception):
    pass


def _recv_exact(conn: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(min(size - len(data), 65536))
        if not chunk:
            raise ConnectionError("connection closed")
        data.extend(chunk)
    return bytes(data)


def recv_frame(conn: socket.socket, idle_timeout=None):
    """Return (type, payload); None for a clean close between frames."""
    conn.settimeout(idle_timeout)
    try:
        first = conn.recv(1)
    except socket.timeout:
        raise
    if not first:
        return None
    conn.settimeout(FRAME_SECONDS)
    header = first + _recv_exact(conn, _HEADER.size - 1)
    length, ftype = _HEADER.unpack(header)
    if length < 1 or length > MAX_FRAME:
        raise ProtocolError(f"frame length {length} out of range")
    return ftype, _recv_exact(conn, length - 1)


def send_frame(conn: socket.socket, ftype: int, payload: bytes = b"") -> None:
    conn.settimeout(FRAME_SECONDS)
    conn.sendall(_HEADER.pack(len(payload) + 1, ftype) + payload)


class RemoteRequest:
    """One remote segment, handed to the recognizer worker."""

    def __init__(self, audio: bytes, peer: str):
        self.audio = audio
        self.peer = peer
        self.text = None
        self.error = None
        self.cancelled = False      # the client went away: skip the work
        self.done = threading.Event()


class RemoteASRServer:
    """Accepts clients on socket_path() and queues their segments."""

    def __init__(self, submit, backend_name):
        # submit(request): put a RemoteRequest into the recognizer queue
        self._submit = submit
        self._backend_name = backend_name
        self._path = socket_path()
        self._sock = None
        self._thread = None
        self._running = False
        self._clients = 0
        self._clients_lock = threading.Lock()
        self._queued = []               # RemoteRequests not yet done

    def start(self) -> None:
        self._path.unlink(missing_ok=True)
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        old = os.umask(0o177)           # the socket is created 0600
        try:
            sock.bind(str(self._path))
        finally:
            os.umask(old)
        sock.listen(MAX_CONNECTIONS)
        sock.settimeout(1.0)
        self._sock = sock
        self._running = True
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()
        print(f"[asr-server] remote recognition on {self._path}", file=sys.stderr)

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._sock:
            self._sock.close()
        self._path.unlink(missing_ok=True)

    def _accept_loop(self) -> None:
        while self._running:
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with self._clients_lock:
                if self._clients >= MAX_CONNECTIONS:
                    full = True
                else:
                    full = False
                    self._clients += 1
            if full:
                try:
                    send_frame(conn, ERROR, b"too many clients")
                except OSError:
                    pass
                conn.close()
                continue
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn: socket.socket) -> None:
        peer = "client"
        try:
            # Peer credentials: who opened the bridge (always this user:
            # the socket is 0600 in a 0700 directory).
            creds = conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED,
                                    struct.calcsize("3i"))
            pid, uid, _ = struct.unpack("3i", creds)
            peer = f"pid {pid}"
            if uid != os.getuid():
                return
            frame = recv_frame(conn, idle_timeout=FRAME_SECONDS)
            if frame is None:
                return
            ftype, payload = frame
            if ftype != HELLO or len(payload) != 2:
                send_frame(conn, ERROR, b"expected HELLO")
                return
            (version,) = struct.unpack("<H", payload)
            if version != PROTOCOL_VERSION:
                send_frame(conn, ERROR,
                           f"protocol {version} not supported, server speaks "
                           f"{PROTOCOL_VERSION}".encode())
                return
            send_frame(conn, INFO,
                       struct.pack("<HIB", PROTOCOL_VERSION, SAMPLE_RATE, CHANNELS)
                       + self._backend_name().encode())
            print(f"[asr-server] {peer} connected", file=sys.stderr)
            while self._running:
                frame = recv_frame(conn)
                if frame is None:
                    break
                ftype, payload = frame
                if ftype != AUDIO:
                    send_frame(conn, ERROR, b"expected AUDIO")
                    break
                if len(payload) % 2:
                    send_frame(conn, ERROR, b"odd number of audio bytes")
                    break
                if not self._answer(conn, RemoteRequest(payload, peer)):
                    break
        except ProtocolError as e:
            print(f"[asr-server] {peer}: {e}", file=sys.stderr)
            try:
                send_frame(conn, ERROR, str(e).encode())
            except OSError:
                pass
        except (OSError, ConnectionError) as e:
            print(f"[asr-server] {peer} dropped: {e}", file=sys.stderr)
        finally:
            conn.close()
            with self._clients_lock:
                self._clients -= 1
            print(f"[asr-server] {peer} disconnected", file=sys.stderr)

    def _answer(self, conn: socket.socket, request: RemoteRequest) -> bool:
        """Queue one segment and reply. False: drop the connection."""
        with self._clients_lock:
            self._queued = [r for r in self._queued if not r.done.is_set()]
            full = len(self._queued) >= MAX_QUEUED
            if not full:
                self._queued.append(request)
        if full:
            send_frame(conn, ERROR, b"server busy: too many segments queued")
            return True
        self._submit(request)
        deadline = time.monotonic() + RECOGNIZE_SECONDS
        while not request.done.wait(0.5):
            # A client that went away while queued: let the worker skip it.
            # (Not recv(MSG_DONTWAIT): a socket with a timeout polls first.)
            readable, _, _ = select.select([conn], [], [], 0)
            gone = False
            if readable:
                conn.settimeout(0)
                try:
                    gone = conn.recv(1, socket.MSG_PEEK) == b""
                except BlockingIOError:
                    pass
                except OSError:
                    gone = True
            if gone:
                request.cancelled = True
                raise ConnectionError("client left while its segment was queued")
            if time.monotonic() > deadline:
                # Still queued: it keeps counting against MAX_QUEUED until
                # the worker skips it; this connection ends so the client
                # cannot pile up more behind it
                request.cancelled = True
                send_frame(conn, ERROR, b"server busy: segment not recognized in time")
                return False
        if request.error is not None:
            send_frame(conn, ERROR, request.error.encode())
        else:
            send_frame(conn, TEXT, request.text.encode())
        return True

