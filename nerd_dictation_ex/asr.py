"""
The recognition protocol and both its ends (docs/client-server.md).

Server (`nerd-dictation server`): loads the ASR backend once and serves
socket_path() to clients of this user - the daemon on this machine
directly, other machines through ssh and the stdio bridge
(scripts/nerd-dictation-asr-bridge). Every segment goes through one worker
thread: one model, in arrival order.

Client (the dictation daemon): AsrClient sends a recorded segment and gets
its text, over the unix socket (host "local") or over one long-lived ssh
connection running the bridge (host = an ssh alias).

The socket is in the user's XDG_RUNTIME_DIR (mode 0700) and is itself 0600,
and the server checks the peer's uid: only this user - locally or through
ssh with this user's keys - can reach it.

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

import base64
import os
import queue
import select
import signal
import socket
import struct
import subprocess
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
# Segments allowed in the worker queue at once, over all clients: bounds the
# memory (x 30 s of audio) and how long one client's segment can wait behind
# the others' (each 30 s segment took ~0.7 s on corentin's GPU).
MAX_QUEUED = MAX_CONNECTIONS
# A client that started a frame must finish it in this time; between frames
# it may idle for as long as its connection lives.
FRAME_SECONDS = 30
# How long a segment may wait for the worker before the client is told the
# server is busy.
RECOGNIZE_SECONDS = 60
# Client side: ssh through a jump host plus the server's own handshake; the
# reply to a segment includes its wait for the worker.
HANDSHAKE_SECONDS = 20
REPLY_SECONDS = RECOGNIZE_SECONDS + 15
RETRY_MIN_SECONDS, RETRY_MAX_SECONDS = 2, 30
# The local server is a socket file: trying it costs nothing, and after its
# restart the client should be back within a second
LOCAL_RETRY_SECONDS = 1
# A segment that finds no link waits this long for the keeper to bring one
# up (it may be reconnecting right now after noticing a drop), then fails
LINK_WAIT_SECONDS = 3

_HEADER = struct.Struct("<IB")

BRIDGE = Path(__file__).resolve().parent.parent / "scripts" / "nerd-dictation-asr-bridge"


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


def recv_frame(conn: socket.socket, idle_timeout=None, frame_timeout=FRAME_SECONDS):
    """Return (type, payload); None for a clean close between frames."""
    conn.settimeout(idle_timeout)
    first = conn.recv(1)
    if not first:
        return None
    conn.settimeout(frame_timeout)
    header = first + _recv_exact(conn, _HEADER.size - 1)
    length, ftype = _HEADER.unpack(header)
    if length < 1 or length > MAX_FRAME:
        raise ProtocolError(f"frame length {length} out of range")
    return ftype, _recv_exact(conn, length - 1)


def send_frame(conn: socket.socket, ftype: int, payload: bytes = b"") -> None:
    conn.settimeout(FRAME_SECONDS)
    conn.sendall(_HEADER.pack(len(payload) + 1, ftype) + payload)


# ============================================================================
# Server
# ============================================================================

class Request:
    """One client's segment, handed to the worker."""

    def __init__(self, audio: bytes, peer: str):
        self.audio = audio
        self.peer = peer
        self.text = None
        self.error = None
        self.cancelled = False      # the client went away: skip the work
        self.done = threading.Event()


class Recognizer:
    """The one thread that calls the backend: the model is not thread-safe,
    and there is one of it."""

    # A streaming backend (t-one) is fed a segment in these steps, as it
    # was fed from the microphone, and its final phrases are collected.
    STREAM_STEP_SECONDS = 0.25

    def __init__(self, backend, backend_name: str):
        self.backend = backend
        self.backend_name = backend_name
        self._queue: "queue.Queue[Request]" = queue.Queue()
        self._running = True
        self._thread = threading.Thread(target=self._loop, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def submit(self, request: Request) -> None:
        self._queue.put(request)

    def stop(self) -> None:
        self._running = False
        self._thread.join(timeout=5.0)

    def _loop(self) -> None:
        while self._running:
            try:
                request = self._queue.get(timeout=0.2)
            except queue.Empty:
                continue
            try:
                if not request.cancelled:
                    self._recognize(request)
            except Exception as e:
                request.error = f"ошибка распознавания: {type(e).__name__}: {e}"
            finally:
                request.done.set()
        # Stopping: whoever still waits gets an answer, not a hang
        while True:
            try:
                request = self._queue.get_nowait()
            except queue.Empty:
                return
            request.error = "сервер останавливается"
            request.done.set()

    def _recognize(self, request: Request) -> None:
        backend = self.backend
        audio = request.audio
        if backend.sample_rate != SAMPLE_RATE:
            # A resampler per segment: segments of different clients are
            # independent, no filter history may pass between them
            from _nerd_dictation_native import audio as native_audio
            audio = native_audio.Resampler(SAMPLE_RATE, backend.sample_rate).process(audio)
        seconds = len(request.audio) / (SAMPLE_RATE * 2)
        started = time.monotonic()
        try:
            if backend.supports_streaming:
                step = int(backend.sample_rate * self.STREAM_STEP_SECONDS) * 2
                phrases = []
                for i in range(0, len(audio), step):
                    backend.feed(audio[i:i + step])
                    result = backend.result()
                    if result.is_final and result.text:
                        phrases.append(result.text)
                tail = backend.finalize().text
                if tail:
                    phrases.append(tail)
                request.text = " ".join(phrases)
            else:
                backend.feed(audio)
                request.text = backend.finalize().text or ""
        finally:
            # A failed finalize leaves its buffer: the next segment, of
            # whichever client, must not inherit it
            backend.reset()
        print(
            f"[server] сегмент {seconds:.2f}s ({request.peer}) распознан за "
            f"{time.monotonic() - started:.3f}s",
            file=sys.stderr,
        )


class AsrServer:
    """Accepts clients on socket_path() and queues their segments."""

    def __init__(self, recognizer: Recognizer):
        self._recognizer = recognizer
        self._path = socket_path()
        self._sock = None
        self._thread = None
        self._running = False
        self._clients = 0
        self._clients_lock = threading.Lock()
        self._queued = []               # Requests not yet done
        self._conns = set()             # open client connections, for stop()

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
        print(f"[server] распознавание на {self._path}", file=sys.stderr)

    def stop(self) -> None:
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
        if self._sock:
            self._sock.close()
        self._path.unlink(missing_ok=True)
        # Connected clients learn it now (EOF) and reconnect, instead of
        # sending a segment to a server that will not answer it
        with self._clients_lock:
            conns = list(self._conns)
        for conn in conns:
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

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
            with self._clients_lock:
                self._conns.add(conn)
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn: socket.socket) -> None:
        peer = "client"
        try:
            # Peer credentials: who connected (always this user: the socket
            # is 0600 in a 0700 directory; checked anyway).
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
                       + self._recognizer.backend_name.encode())
            print(f"[server] {peer} подключился", file=sys.stderr)
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
                if not self._answer(conn, Request(payload, peer)):
                    break
        except ProtocolError as e:
            print(f"[server] {peer}: {e}", file=sys.stderr)
            try:
                send_frame(conn, ERROR, str(e).encode())
            except OSError:
                pass
        except (OSError, ConnectionError) as e:
            print(f"[server] {peer} оборвался: {e}", file=sys.stderr)
        finally:
            conn.close()
            with self._clients_lock:
                self._clients -= 1
                self._conns.discard(conn)
            print(f"[server] {peer} отключился", file=sys.stderr)

    def _answer(self, conn: socket.socket, request: Request) -> bool:
        """Queue one segment and reply. False: drop the connection."""
        with self._clients_lock:
            self._queued = [r for r in self._queued if not r.done.is_set()]
            full = len(self._queued) >= MAX_QUEUED
            if not full:
                self._queued.append(request)
        if full:
            send_frame(conn, ERROR, b"server busy: too many segments queued")
            return True
        self._recognizer.submit(request)
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


def run_server(config: dict) -> None:
    """`nerd-dictation server`: load the backend, serve until SIGTERM/SIGINT."""
    from .backends import get_backend

    name = config.get("server", {}).get("backend")
    if not name:
        sys.exit('сервер: в конфиге нет [server] backend = "<бэкенд>"')
    print(f"[server] загрузка бэкенда {name}...", file=sys.stderr)
    # The model is embedded in the .so: loading the backend loads the model
    backend = get_backend(name)
    recognizer = Recognizer(backend, name)
    recognizer.start()
    server = AsrServer(recognizer)

    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    server.start()
    print(f"[server] бэкенд {name} загружен, сервер готов", file=sys.stderr)
    try:
        while not stop.wait(1.0):
            pass
    finally:
        server.stop()
        recognizer.stop()
        print("[server] остановлен", file=sys.stderr)


# ============================================================================
# Client
# ============================================================================

class _Link:
    """One connection to the server: the unix socket itself, or the stdio of
    `ssh <host>` running the bridge (a socketpair, so a dead peer is an
    error, not a SIGPIPE)."""

    STDERR_KEEP = 2048

    def __init__(self, sock: socket.socket, proc=None):
        self.sock = sock
        self.proc = proc
        self.server = ""            # the server's backend name from INFO
        self._stderr = b""
        if proc and proc.stderr:
            # Drained all the time: ssh blocked on a full stderr pipe would
            # stall the link; the tail is kept for error messages
            threading.Thread(target=self._drain, args=(proc.stderr,), daemon=True).start()

    def _drain(self, pipe) -> None:
        try:
            while True:
                chunk = os.read(pipe.fileno(), 4096)
                if not chunk:
                    return
                self._stderr = (self._stderr + chunk)[-self.STDERR_KEEP:]
        except (OSError, ValueError):
            return

    def stderr(self) -> str:
        """What ssh or the bridge said lately (its last lines)."""
        if self.proc and self.proc.stderr:
            time.sleep(0.05)        # let the drain thread catch a last message
        lines = [l for l in self._stderr.decode(errors="replace").splitlines()
                 # ssh's advisory, printed on every connection to some hosts
                 if "post-quantum" not in l and "upgrade.html" not in l]
        return " ".join(l.strip() for l in lines if l.strip())

    def exited(self) -> bool:
        """The link is gone: ssh exited, or the server closed the socket.
        Between segments the server never sends anything, so a readable
        socket there means EOF (or a stray byte - also not a usable link)."""
        if self.proc is not None and self.proc.poll() is not None:
            return True
        try:
            readable, _, _ = select.select([self.sock], [], [], 0)
        except (OSError, ValueError):
            return True
        return bool(readable)

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass
        if self.proc:
            # TERM, up to 2 s to go, then KILL: a stuck ssh must not hang
            # the caller (the daemon's worker, or its shutdown)
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
            self.proc = None


class AsrClient:
    """Sends segments to the server named by `[server] host` and returns the
    text. A keeper thread connects at start and reconnects with backoff
    whenever the link is down; recognize() fails at once, with the reason,
    while there is no link."""

    def __init__(self, host: str):
        self.host = host
        self._lock = threading.Lock()      # guards _link and _failure
        self._link = None
        self._failure = ""
        self._stopping = threading.Event()
        self._wake = threading.Event()
        self._connecting = None            # the link being handshaken, for stop()
        self._thread = threading.Thread(target=self._keep, daemon=True)

    @property
    def label(self) -> str:
        return "local server" if self.host == "local" else self.host

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stopping.set()
        self._wake.set()
        connecting = self._connecting
        if connecting is not None:
            # A handshake in progress ends when its socket (and ssh) does
            try:
                connecting.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            if connecting.proc:
                connecting.proc.terminate()
        self._thread.join(timeout=5.0)
        with self._lock:
            if self._link:
                self._link.close()
                self._link = None

    def status(self):
        """(connected, the server's backend name or the reason it is not).
        Without the lock: a segment being recognized holds it, and the
        dictation toggle asking this must not wait for that."""
        link = self._link
        if link:
            return True, link.server
        return False, self._failure or f"{self.label}: not connected yet"

    # -- connecting ----------------------------------------------------------

    def _open(self):
        """Connect and handshake (without the lock: it may take seconds).
        Returns (link, None) or (None, reason)."""
        if self.host == "local":
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.connect(str(socket_path()))
            except OSError as e:
                sock.close()
                return None, f"нет сервера распознавания на {socket_path()}: {e}"
            link = _Link(sock)
        else:
            ours, theirs = socket.socketpair()
            bridge = ("python3 -c \"import base64;exec(base64.b64decode('"
                      + base64.b64encode(BRIDGE.read_bytes()).decode() + "'))\"")
            try:
                proc = subprocess.Popen(
                    ["ssh", "-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
                     "-o", "ServerAliveInterval=10", "-o", "ServerAliveCountMax=3",
                     self.host, bridge],
                    stdin=theirs, stdout=theirs, stderr=subprocess.PIPE,
                    # Its own process group: a Ctrl+C meant for the daemon's
                    # terminal must not kill the link
                    start_new_session=True)
            except OSError as e:
                ours.close()
                theirs.close()
                return None, f"cannot run ssh: {e}"
            theirs.close()
            link = _Link(ours, proc)
        self._connecting = link
        if self._stopping.is_set():
            # stop() ran before it could see this link: end it here
            self._connecting = None
            link.close()
            return None, f"{self.label}: shutting down"
        try:
            send_frame(link.sock, HELLO, struct.pack("<H", PROTOCOL_VERSION))
            frame = recv_frame(link.sock, idle_timeout=HANDSHAKE_SECONDS,
                               frame_timeout=HANDSHAKE_SECONDS)
        except (OSError, ConnectionError, ProtocolError) as e:
            frame = None
            why = link.stderr() or str(e)
        else:
            why = link.stderr()
        finally:
            self._connecting = None
        if frame is None:
            link.close()
            return None, f"{self.label} недоступен" + (f": {why}" if why else "")
        ftype, payload = frame
        if ftype == ERROR:
            link.close()
            return None, f"{self.label}: {payload.decode(errors='replace')}"
        if (ftype != INFO or len(payload) < 7
                or struct.unpack("<HIB", payload[:7])[:2] != (PROTOCOL_VERSION, SAMPLE_RATE)):
            link.close()
            return None, f"{self.label}: unexpected handshake reply"
        link.server = payload[7:].decode(errors="replace")
        print(f"[asr] подключён к {self.label} (бэкенд {link.server})", file=sys.stderr)
        return link, None

    def _reconnect(self) -> bool:
        """Replace the link with a fresh one. Called without the lock."""
        link, failure = self._open()
        with self._lock:
            if self._link:
                self._link.close()
                self._link = None
            if link and not self._stopping.is_set():
                self._link = link
                self._failure = ""
                return True
            if link:
                link.close()
            self._failure = failure or f"{self.label}: shutting down"
            return False

    def _keep(self) -> None:
        delay = RETRY_MIN_SECONDS
        while not self._stopping.is_set():
            with self._lock:
                if self._link and self._link.exited():
                    why = self._link.stderr()
                    self._link.close()
                    self._link = None
                    self._failure = f"{self.label}: связь закрылась" + (f": {why}" if why else "")
                    print(f"[asr] {self._failure}", file=sys.stderr)
                up = self._link is not None
            wait = 1.0
            if not up:
                if self._reconnect():
                    delay = RETRY_MIN_SECONDS
                elif not self._stopping.is_set():
                    if self.host == "local":
                        wait = LOCAL_RETRY_SECONDS
                        if delay == RETRY_MIN_SECONDS:    # once per outage
                            print(f"[asr] {self._failure}; повтор каждую секунду",
                                  file=sys.stderr)
                        delay = RETRY_MAX_SECONDS
                    else:
                        print(f"[asr] {self._failure}; повтор через {delay} s",
                              file=sys.stderr)
                        wait = delay
                        delay = min(delay * 2, RETRY_MAX_SECONDS)
            self._wake.wait(wait)
            self._wake.clear()

    # -- recognizing -----------------------------------------------------------

    def _request(self, audio: bytes):
        """Send one segment on the current link and read the reply. Caller
        holds the lock. Returns ("text", t), ("error", message) - the link is
        fine - or None: the link broke (it is closed then)."""
        link = self._link
        reason = "connection closed"
        try:
            send_frame(link.sock, AUDIO, audio)
            frame = recv_frame(link.sock, idle_timeout=REPLY_SECONDS,
                               frame_timeout=REPLY_SECONDS)
        except (OSError, ConnectionError, ProtocolError) as e:
            frame = None
            reason = str(e)
        if frame is None:
            why = link.stderr() or reason
            link.close()
            self._link = None
            self._failure = f"{self.label}: связь потеряна" + (f": {why}" if why else "")
            return None
        ftype, payload = frame
        text = payload.decode(errors="replace")
        if ftype == TEXT:
            return "text", text
        return "error", text if ftype == ERROR else "unexpected reply"

    def recognize(self, audio: bytes):
        """Text of one 16 kHz int16 segment, or raise RuntimeError with the
        reason (then nothing is typed)."""
        if len(audio) > MAX_SEGMENT_SECONDS * SAMPLE_RATE * 2:
            raise RuntimeError(f"сегмент длиннее {MAX_SEGMENT_SECONDS} s")
        if self._link is None:
            # The keeper may be reconnecting right now (it noticed a drop):
            # give it a moment, not a whole stuck handshake
            self._wake.set()
            end = time.monotonic() + LINK_WAIT_SECONDS
            while self._link is None and time.monotonic() < end \
                    and not self._stopping.is_set():
                time.sleep(0.05)
        with self._lock:
            had_link = self._link is not None
            reply = self._request(audio) if had_link else None
        if reply is None and had_link and not self._stopping.is_set():
            # A link that died since the last segment (sleep, a network
            # change, a server restart) unnoticed: one immediate reconnect
            # and retry
            if self._reconnect():
                with self._lock:
                    reply = self._request(audio) if self._link else None
        self._wake.set()        # the keeper notices a dropped link at once
        if reply is None:
            with self._lock:
                failure = self._failure or f"{self.label}: not connected yet"
            raise RuntimeError(failure)
        kind, text = reply
        if kind == "error":
            raise RuntimeError(f"{self.label}: {text}")
        return text
