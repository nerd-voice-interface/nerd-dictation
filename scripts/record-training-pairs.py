#!/usr/bin/env python3
"""
Record training pairs (audio + transcription) for speech recognition testing.

Usage:
    python scripts/record-training-pairs.py [--dir OUTPUT_DIR] [--name SAMPLE_NAME]
    python scripts/record-training-pairs.py --list-sources
    python scripts/record-training-pairs.py --source "Built-in Audio Analog Stereo"

Features:
    - Record audio of arbitrary length (Enter to start, Enter to stop)
    - Uses pw-cat (PipeWire) for best quality - same as nerd-dictation
    - Input text transcription after recording
    - Optional sample name (default: timestamp)
    - Metadata stored in samples.json
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

try:
    import numpy as np
except ImportError:
    print("ERROR: numpy not installed!")
    print("  pip install numpy")
    sys.exit(1)


# Audio settings (same as nerd-dictation)
SAMPLE_RATE = 16000
CHANNELS = 1
BYTES_PER_SAMPLE = 2  # s16 = 2 bytes

# Default output directory
DEFAULT_OUTPUT_DIR = "training-samples"
METADATA_FILE = "samples.json"


def get_available_sources() -> list[dict]:
    """Get list of available audio sources from PipeWire."""
    try:
        result = subprocess.run(
            ["pactl", "list", "sources", "short"],
            capture_output=True, text=True, timeout=5
        )
        sources = []
        for line in result.stdout.strip().split('\n'):
            if line:
                parts = line.split('\t')
                if len(parts) >= 2:
                    sources.append({
                        "id": parts[0],
                        "name": parts[1],
                        "driver": parts[2] if len(parts) > 2 else "",
                        "format": parts[3] if len(parts) > 3 else "",
                        "state": parts[4] if len(parts) > 4 else ""
                    })
        return sources
    except Exception as e:
        print(f"Warning: Could not list sources: {e}", file=sys.stderr)
        return []


def get_default_source() -> str:
    """Get default PipeWire source name."""
    try:
        result = subprocess.run(
            ["pactl", "get-default-source"],
            capture_output=True, text=True, timeout=5
        )
        return result.stdout.strip()
    except Exception:
        return ""


class PipeWireRecorder:
    """Audio recorder using pw-cat (PipeWire) - same method as nerd-dictation."""

    def __init__(self, sample_rate: int = SAMPLE_RATE, source: str = None):
        self.sample_rate = sample_rate
        self.source = source
        self.process = None
        self.audio_data = []
        self.recording = False
        self._stop_event = threading.Event()

    def _build_command(self) -> list[str]:
        """Build pw-cat command."""
        cmd = [
            "pw-cat", "--record", "--raw",  # no .snd container on stdout
            "--format", "s16",
            "--rate", str(self.sample_rate),
            "--channels", "1",
        ]
        if self.source:
            cmd.extend(["--target", self.source])
        cmd.append("-")  # Output to stdout
        return cmd

    def _reader_thread(self):
        """Read audio data from pw-cat stdout."""
        chunk_size = self.sample_rate * BYTES_PER_SAMPLE // 10  # 100ms chunks
        while self.recording and not self._stop_event.is_set():
            try:
                chunk = self.process.stdout.read(chunk_size)
                if chunk:
                    self.audio_data.append(chunk)
                else:
                    break
            except Exception:
                break

    def record(self) -> tuple[np.ndarray, float]:
        """
        Record audio until user presses Enter.

        Returns:
            Tuple of (audio_data, duration_seconds)
        """
        self.audio_data = []
        self._stop_event.clear()
        self.recording = True

        cmd = self._build_command()

        print("\n🎤 Recording... (press Enter to stop)")
        start_time = time.time()

        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL
            )

            # Start reader thread
            reader = threading.Thread(target=self._reader_thread, daemon=True)
            reader.start()

            # Wait for Enter key
            input()

        except FileNotFoundError:
            print("ERROR: pw-cat not found! Install pipewire-pulse or use parec.", file=sys.stderr)
            return np.array([], dtype=np.int16), 0.0
        finally:
            self.recording = False
            self._stop_event.set()

            if self.process:
                self.process.terminate()
                try:
                    self.process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    self.process.kill()

        duration = time.time() - start_time

        if not self.audio_data:
            return np.array([], dtype=np.int16), 0.0

        # Concatenate all audio chunks
        raw_audio = b''.join(self.audio_data)
        audio = np.frombuffer(raw_audio, dtype=np.int16)
        return audio, duration


def load_metadata(metadata_path: Path) -> dict:
    """Load existing metadata or create empty structure."""
    if metadata_path.exists():
        with open(metadata_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    return {"samples": [], "version": 1}


def save_metadata(metadata_path: Path, metadata: dict):
    """Save metadata to JSON file."""
    with open(metadata_path, 'w', encoding='utf-8') as f:
        json.dump(metadata, f, ensure_ascii=False, indent=2)


def generate_sample_name() -> str:
    """Generate sample name from current timestamp."""
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def save_wav(filepath: Path, audio: np.ndarray, sample_rate: int):
    """Save audio data to WAV file without soundfile dependency."""
    import struct
    import wave

    with wave.open(str(filepath), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)  # 16-bit
        wav.setframerate(sample_rate)
        wav.writeframes(audio.tobytes())


def record_single_sample(
    output_dir: Path,
    metadata: dict,
    sample_name: str | None = None,
    transcription: str | None = None,
    source: str | None = None
) -> bool:
    """
    Record a single training sample.

    Args:
        output_dir: Directory to save audio files
        metadata: Metadata dict to update
        sample_name: Optional sample name (default: timestamp)
        transcription: Optional transcription (if None, will prompt)
        source: Optional audio source name

    Returns:
        True if sample was recorded successfully
    """
    recorder = PipeWireRecorder(source=source)

    # Generate name if not provided
    if not sample_name:
        sample_name = generate_sample_name()

    # Sanitize name for filename
    safe_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in sample_name)
    wav_filename = f"{safe_name}.wav"
    wav_path = output_dir / wav_filename

    # Check if file already exists
    if wav_path.exists():
        print(f"⚠️  File already exists: {wav_filename}")
        overwrite = input("Overwrite? [y/N]: ").strip().lower()
        if overwrite != 'y':
            return False

    print(f"\n{'='*50}")
    print(f"Recording sample: {sample_name}")
    print(f"{'='*50}")

    try:
        input("Press Enter when ready to start recording...")
    except KeyboardInterrupt:
        print("\nCancelled.")
        return False

    # Record audio
    try:
        audio_data, duration = recorder.record()
    except KeyboardInterrupt:
        print("\nRecording interrupted.")
        return False

    if duration < 0.5:
        print("⚠️  Recording too short (< 0.5s), skipping.")
        return False

    print(f"✓ Recorded {duration:.1f} seconds")

    # Get transcription if not provided
    if transcription is None:
        print("\nEnter the exact text you spoke:")
        try:
            transcription = input("> ").strip()
        except KeyboardInterrupt:
            print("\nCancelled.")
            return False

    if not transcription:
        print("⚠️  Empty transcription, skipping.")
        return False

    # Save audio file
    try:
        save_wav(wav_path, audio_data, SAMPLE_RATE)
        file_size = wav_path.stat().st_size
        print(f"✓ Saved: {wav_filename} ({file_size:,} bytes)")
    except Exception as e:
        print(f"✗ Error saving audio: {e}")
        return False

    # Add to metadata
    sample_entry = {
        "name": sample_name,
        "file": wav_filename,
        "transcription": transcription,
        "duration": round(duration, 2),
        "sample_rate": SAMPLE_RATE,
        "recorded_at": datetime.now().isoformat()
    }

    # Check for existing entry with same name and update
    existing_idx = None
    for i, s in enumerate(metadata["samples"]):
        if s["name"] == sample_name:
            existing_idx = i
            break

    if existing_idx is not None:
        metadata["samples"][existing_idx] = sample_entry
        print(f"✓ Updated metadata for: {sample_name}")
    else:
        metadata["samples"].append(sample_entry)
        print(f"✓ Added to metadata: {sample_name}")

    return True


def interactive_session(output_dir: Path, source: str = None):
    """Run interactive recording session."""
    metadata_path = output_dir / METADATA_FILE
    metadata = load_metadata(metadata_path)

    # Show source info
    default_source = get_default_source()
    active_source = source or default_source

    print("="*60)
    print("  Training Pairs Recorder (pw-cat)")
    print("="*60)
    print(f"\nOutput directory: {output_dir.absolute()}")
    print(f"Metadata file: {metadata_path.name}")
    print(f"Existing samples: {len(metadata['samples'])}")
    print(f"\nAudio: {SAMPLE_RATE} Hz, mono, 16-bit (same as nerd-dictation)")
    print(f"Source: {active_source}")
    print("\nCommands:")
    print("  [Enter]     - Record new sample (auto-generated name)")
    print("  [name]      - Record sample with custom name")
    print("  list        - List all recorded samples")
    print("  sources     - Show available audio sources")
    print("  delete NAME - Delete a sample")
    print("  quit/q      - Exit")
    print("-"*60)

    while True:
        try:
            cmd = input("\n> ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\n")
            break

        if not cmd or cmd.lower() in ('record', 'r'):
            # Record with auto-generated name
            if record_single_sample(output_dir, metadata, source=source):
                save_metadata(metadata_path, metadata)

        elif cmd.lower() in ('quit', 'q', 'exit'):
            break

        elif cmd.lower() == 'list':
            if not metadata["samples"]:
                print("No samples recorded yet.")
            else:
                print(f"\nRecorded samples ({len(metadata['samples'])}):")
                for s in metadata["samples"]:
                    print(f"  • {s['name']}: \"{s['transcription'][:40]}...\" ({s['duration']:.1f}s)"
                          if len(s['transcription']) > 40
                          else f"  • {s['name']}: \"{s['transcription']}\" ({s['duration']:.1f}s)")

        elif cmd.lower() == 'sources':
            sources = get_available_sources()
            default = get_default_source()
            print("\nAvailable audio sources:")
            for s in sources:
                marker = " *" if s["name"] == default else ""
                print(f"  {s['name']}{marker}")
                print(f"    Format: {s['format']}, State: {s['state']}")
            print(f"\n  * = default source")
            print(f"\nTip: Use --source NAME to select a specific source")

        elif cmd.lower().startswith('delete '):
            name_to_delete = cmd[7:].strip()
            found = False
            for i, s in enumerate(metadata["samples"]):
                if s["name"] == name_to_delete:
                    # Delete audio file
                    wav_path = output_dir / s["file"]
                    if wav_path.exists():
                        wav_path.unlink()
                        print(f"✓ Deleted: {s['file']}")
                    # Remove from metadata
                    metadata["samples"].pop(i)
                    save_metadata(metadata_path, metadata)
                    print(f"✓ Removed from metadata: {name_to_delete}")
                    found = True
                    break
            if not found:
                print(f"Sample not found: {name_to_delete}")

        else:
            # Treat input as custom sample name
            if record_single_sample(output_dir, metadata, sample_name=cmd, source=source):
                save_metadata(metadata_path, metadata)

    # Final save
    save_metadata(metadata_path, metadata)
    print(f"\n✓ Session complete. Total samples: {len(metadata['samples'])}")


def main():
    parser = argparse.ArgumentParser(
        description="Record training pairs (audio + transcription) for speech recognition"
    )
    parser.add_argument(
        '--dir', '-d',
        type=Path,
        default=Path(DEFAULT_OUTPUT_DIR),
        help=f"Output directory for samples (default: {DEFAULT_OUTPUT_DIR})"
    )
    parser.add_argument(
        '--name', '-n',
        type=str,
        help="Record single sample with given name and exit"
    )
    parser.add_argument(
        '--text', '-t',
        type=str,
        help="Transcription text (for non-interactive single recording)"
    )
    parser.add_argument(
        '--source', '-s',
        type=str,
        help="Audio source name (use --list-sources to see available)"
    )
    parser.add_argument(
        '--list-sources', '-l',
        action='store_true',
        help="List available audio sources and exit"
    )

    args = parser.parse_args()

    # List sources mode
    if args.list_sources:
        sources = get_available_sources()
        default = get_default_source()
        print("Available audio sources:\n")
        for s in sources:
            marker = " <- DEFAULT" if s["name"] == default else ""
            # Highlight non-bluetooth sources as recommended
            quality = ""
            if "bluez" in s["name"].lower():
                quality = " (Bluetooth - may have poor mic quality!)"
            elif "alsa_input" in s["name"]:
                quality = " (Built-in - recommended)"
            print(f"  {s['name']}{marker}{quality}")
            print(f"    Format: {s['format']}")
        print(f"\nUsage: {sys.argv[0]} --source 'alsa_input.pci-0000_00_1f.3.analog-stereo'")
        return

    # Create output directory
    args.dir.mkdir(parents=True, exist_ok=True)

    if args.name:
        # Single recording mode
        metadata_path = args.dir / METADATA_FILE
        metadata = load_metadata(metadata_path)
        if record_single_sample(args.dir, metadata, sample_name=args.name,
                               transcription=args.text, source=args.source):
            save_metadata(metadata_path, metadata)
    else:
        # Interactive session
        interactive_session(args.dir, source=args.source)


if __name__ == "__main__":
    main()
