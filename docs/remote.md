# Remote recognition

A machine without a usable GPU (yoga3, 3a5000, ...) can dictate with the
model of another machine's dictation daemon (corentin: GigaAM on its GPU).

```
client (yoga3)                                  server (corentin)
 microphone -> VAD -> segment                    nerd-dictation daemon
   backend "remote" (libnerd-dictation-remote)     recognizer worker (one model)
     feed: collect the segment                       ^ local segments
     finalize: AUDIO frame ----- ssh ----> bridge -> nerd-dictation-asr.sock
               TEXT frame  <----------------------- (remote_server.py)
 postprocess, voice commands, typing (local)
```

- **Client**: `[general] backend = "remote"` and `[remote] host = "<ssh
  alias>"` in `~/.config/nerd-dictation/config.toml`. Recording, VAD,
  postprocessing, voice commands and typing stay local; only the audio of a
  finished segment (16 kHz mono int16, at most 30 s) leaves the machine, and
  only its text comes back.
- **Server**: every daemon whose backend is not `remote` also serves
  `$XDG_RUNTIME_DIR/nerd-dictation-asr.sock` (0600). Remote segments go
  through the same single recognizer worker as the local microphone, in
  arrival order: one model on the GPU, never two. A streaming backend
  (t-one) refuses remote segments: its phrase state belongs to the local
  session.
- **Transport**: one long-lived ssh connection per client, running a stdio
  bridge to the socket (`nerd-dictation-remote/src/bridge.py`, sent as
  base64 so any login shell passes it). ssh is the only authentication and
  encryption; it goes through ProxyJump hosts like any other ssh. No TCP
  port is opened. For an unattended client use a dedicated key that may
  only start the bridge: in the server's `authorized_keys`,
  `restrict,command="~/.local/bin/nerd-dictation-asr-bridge" ssh-ed25519 ...`
  (yoga3: alias `corentin-asr`).
- **Offline**: if the server is unreachable or its daemon is down, finalize
  fails at once with the reason (journal: `[remote] ... - nothing typed`) and
  nothing is typed; a keeper thread reconnects with backoff (2-30 s). A link
  that died between segments gets one immediate reconnect and retry.
- **Protocol v1**: little-endian frames `u32 length | u8 type | payload`;
  HELLO(u16 version) -> INFO(u16 version, u32 rate, u8 channels, backend
  name) | ERROR, then AUDIO -> TEXT | ERROR, one segment in flight per
  connection; frames are capped at 30 s of audio, at most 8 clients.

Worst case for the local microphone: its segment waits behind the remote
segments already queued, at most one per connected client, each up to 30 s
of audio (25 s took 0.68 s on corentin's Quadro P500 at full clock).
