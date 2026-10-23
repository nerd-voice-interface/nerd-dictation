# Client and server

Dictation is split into two programs that speak one protocol, whether they
run on the same machine or not.

```
client (every machine with a microphone)        server (machines with a recognizer)
  nerd-dictation daemon                           nerd-dictation server
  microphone -> VAD -> segment                    the ASR backend (C API .so, e.g.
  AsrClient --- unix socket (host = "local") -->    GigaAM on the GPU), loaded once
            \-- ssh <alias> -> bridge ---------->  $XDG_RUNTIME_DIR/nerd-dictation-asr.sock
  <------------------- text ------------------     one worker: segments of all
  spelling, postprocess, voice commands,           clients in arrival order
  typing, toggle socket, triggers, wake word
```

- **Server** (`nerd-dictation server`, user unit `nerd-dictation-server`):
  loads the backend named by `[server] backend` once and serves
  `$XDG_RUNTIME_DIR/nerd-dictation-asr.sock` (0600, peer uid checked).
  Every segment, from this machine or another, goes through one worker
  thread: one model on the GPU, never two. Nothing about audio capture,
  VAD, typing or commands lives here. A backend at another sample rate
  gets each segment resampled on its own; a streaming backend (t-one) is
  fed the whole segment and finalized, like an offline one.
- **Client** (`nerd-dictation daemon`, user unit `nerd-dictation`): records,
  cuts segments with the VAD, sends each to the server and types the text
  that comes back. It loads no backend. The only difference between local
  and remote is `[server] host`:

  ```toml
  [server]
  host = "local"           # the server on this machine (its unix socket)
  # host = "corentin-asr"  # an ssh alias of the machine running the server
  ```

- **Transport** (`nerd_dictation_ex/asr.py`, both sides): local = the unix
  socket directly; remote = one long-lived `ssh -T <alias>` running the
  stdio bridge to that socket (`scripts/nerd-dictation-asr-bridge`, sent as
  base64 so any login shell passes it; a key restricted to
  `command="~/.local/bin/nerd-dictation-asr-bridge"` runs it regardless).
  ssh is the only authentication and encryption; no TCP port is opened.
- **Offline**: while the server is unreachable, `begin` answers with the
  reason (shown in tty-i3-keys' bar) and a segment already recorded fails
  at once with the reason in the journal (`[asr] ... - nothing typed`);
  nothing is typed. A keeper thread reconnects with backoff (2-30 s); a
  link that died between segments gets one immediate reconnect and retry.
- **Protocol v1** (unchanged): little-endian frames
  `u32 length | u8 type | payload`; HELLO(u16 version) -> INFO(u16
  version, u32 rate = 16000, u8 channels = 1, backend name) | ERROR, then
  AUDIO (int16, 16 kHz mono, one segment, <= 30 s) -> TEXT | ERROR, one
  segment in flight per connection; at most 8 clients and 8 queued
  segments.

Worst case for one client: its segment waits behind the segments already
queued, at most one per connected client, each up to 30 s of audio (25 s
took 0.68 s on corentin's Quadro P500 at full clock).

## What the split removed

- The in-process recognizer in the daemon (`load_backend`, the recognizer
  worker calling `feed/finalize`, the per-session resampler, the `reload`
  command) and the daemon's embedded server (`remote_server.py`): the
  daemon is now only a client.
- The streaming dictation path (chunks fed to t-one as they came, without
  VAD): every client segments with the VAD; t-one on a server recognizes
  whole segments.
- The `remote` backend plugin (`nerd-dictation-remote`, C++): its transport
  and the protocol now live once, in Python, on both sides, and the client
  loads no plugin at all. `[general] backend` and `[remote] host` are gone
  from the config; `[server] backend` / `[server] host` replace them.
