"""VAD segmentation for offline (non-streaming) backends.

The daemon feeds microphone chunks; the segmenter cuts the audio at a pause
after speech (or when the buffer is full) and returns the segments. It is a
class of its own so a recording can be replayed through exactly the code the
daemon runs (tools/replay_vad.py).

With a debug directory (config [debug] audio_dir) it logs, every 0.25 s, the
level of the audio and the highest Silero speech probability, logs every cut
with its reason, and saves each segment as a WAV there.
"""
import math
import os
import sys
import time
import wave
from typing import List, Optional, Tuple

import numpy as np


class VadSegmenter:
    VAD_THRESHOLD = 0.5      # Silero speech probability counted as speech
    MIN_SILENCE_MS = 500     # pause that ends a segment
    MIN_SPEECH_MS = 100      # speech needed before a pause can end a segment
    MAX_BUFFER_SECONDS = 25  # cut anyway at this length
    MIN_SEGMENT_SECONDS = 0.5

    def __init__(self, vad, sample_rate: int, debug_dir: Optional[str] = None,
                 tag: str = ""):
        self.vad = vad
        self.sample_rate = sample_rate
        self.chunk = vad.get_chunk_size()
        self.min_silence = int(self.MIN_SILENCE_MS * sample_rate / 1000)
        self.min_speech = int(self.MIN_SPEECH_MS * sample_rate / 1000)
        self.max_bytes = self.MAX_BUFFER_SECONDS * sample_rate * 2
        self.min_bytes = int(self.MIN_SEGMENT_SECONDS * sample_rate * 2)
        self.audio = bytearray()
        self.pending = np.array([], dtype=np.int16)
        self.silence = self.speech = 0
        self.in_speech = False
        # debug
        self.debug_dir = debug_dir
        self.tag = tag or time.strftime("%H%M%S")
        self.count = 0
        self.position = 0                  # samples fed so far
        self.window = []                   # (rms of a VAD chunk, prob)
        if debug_dir:
            os.makedirs(debug_dir, exist_ok=True)

    def _reset(self) -> None:
        self.silence = self.speech = 0
        self.in_speech = False
        self.pending = np.array([], dtype=np.int16)

    def feed(self, chunk: bytes) -> List[Tuple[bytes, str]]:
        """Feed microphone audio (int16); return the segments it completes."""
        out = []
        self.audio.extend(chunk)
        self.pending = np.concatenate([self.pending, np.frombuffer(chunk, dtype=np.int16)])
        while len(self.pending) >= self.chunk:
            part, self.pending = self.pending[:self.chunk], self.pending[self.chunk:]
            prob = self.vad.process_chunk(part.tobytes()).speech_probability
            if prob >= self.VAD_THRESHOLD:
                self.speech += self.chunk
                self.silence = 0
                self.in_speech = True
            else:
                self.silence += self.chunk
            self.position += self.chunk
            if self.debug_dir:
                self._trace(part, prob)
        if self.in_speech and self.silence >= self.min_silence and self.speech >= self.min_speech:
            out += self._cut("pause")
        elif len(self.audio) >= self.max_bytes:
            out += self._cut("max-length")
        return out

    def flush(self) -> List[Tuple[bytes, str]]:
        """End of dictation: whatever is buffered is a segment too."""
        return self._cut("stop")

    def _cut(self, reason: str) -> List[Tuple[bytes, str]]:
        segment, self.audio = bytes(self.audio), bytearray()
        self._reset()
        seconds = len(segment) / (self.sample_rate * 2)
        if len(segment) < self.min_bytes:
            if self.debug_dir:
                print(f"[segment] {seconds:.2f}s dropped (< {self.MIN_SEGMENT_SECONDS}s), "
                      f"reason={reason}", file=sys.stderr)
            return []
        if self.debug_dir:
            self.count += 1
            path = os.path.join(self.debug_dir, f"{self.tag}-{self.count:03d}.wav")
            with wave.open(path, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(self.sample_rate)
                w.writeframes(segment)
            print(f"[segment] #{self.count} {seconds:.2f}s, reason={reason}, saved {path}",
                  file=sys.stderr)
            return [(segment, path)]
        return [(segment, reason)]

    def _trace(self, part: np.ndarray, prob: float) -> None:
        rms = float(np.sqrt(np.mean(part.astype(np.float64) ** 2)))
        self.window.append((rms, prob))
        if len(self.window) * self.chunk >= self.sample_rate // 4:
            level = math.sqrt(sum(r * r for r, _ in self.window) / len(self.window))
            dbfs = 20 * math.log10(max(level, 1.0) / 32768)
            top = max(p for _, p in self.window)
            t = self.position / self.sample_rate
            state = "speech" if self.in_speech else "-"
            print(f"[vad] t={t:6.2f}s level={dbfs:6.1f} dBFS p_max={top:.2f} "
                  f"silence={self.silence / self.sample_rate:.2f}s {state}", file=sys.stderr)
            self.window = []
