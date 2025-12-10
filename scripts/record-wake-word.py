#!/usr/bin/env python3
"""
Simple script to record wake word "запись" for testing.
Uses sounddevice library for simple and reliable audio recording.
"""

import os
import sys

try:
    import sounddevice as sd
    import soundfile as sf
except ImportError:
    print("ERROR: Required Python packages not installed!")
    print("\nInstall them with:")
    print("  pip install sounddevice soundfile")
    print("\nOr on Arch Linux:")
    print("  sudo pacman -S python-sounddevice python-soundfile")
    sys.exit(1)

SAMPLES_DIR = "test-samples"
SAMPLE_RATE = 8000  # Match t-one backend sample rate
CHANNELS = 1
DURATION = 3  # seconds

def record_wake_word():
    """Record wake word 'запись'."""
    os.makedirs(SAMPLES_DIR, exist_ok=True)
    filepath = os.path.join(SAMPLES_DIR, "wake-word-zapis.wav")
    
    print("="*50)
    print("Recording Wake Word Sample")
    print("="*50)
    print(f"\nSample file: {filepath}")
    print(f"Phrase to say: \"запись\"")
    print(f"Sample rate: {SAMPLE_RATE} Hz")
    print(f"Duration: {DURATION} seconds")
    print(f"\n{'='*50}")
    
    try:
        input("\nPress ENTER when ready to record (or Ctrl+C to cancel)...")
    except KeyboardInterrupt:
        print("\nCancelled.")
        return False
    
    print(f"\nRecording... ({DURATION} seconds)")
    print("Say 'запись' now!")
    
    try:
        # Record audio
        audio_data = sd.rec(
            int(SAMPLE_RATE * DURATION),
            samplerate=SAMPLE_RATE,
            channels=CHANNELS,
            dtype='int16'
        )
        sd.wait()  # Wait until recording is finished
        
        # Save to WAV file
        sf.write(filepath, audio_data, SAMPLE_RATE)
        
        file_size = os.path.getsize(filepath)
        print(f"\n✓ Saved: {filepath} ({file_size:,} bytes)")
        print(f"✓ Sample rate: {SAMPLE_RATE} Hz")
        print(f"✓ Duration: {len(audio_data) / SAMPLE_RATE:.2f} seconds")
        print(f"\nYou can now test wake word detection with this file.")
        return True
        
    except KeyboardInterrupt:
        print("\nRecording interrupted.")
        return False
    except Exception as e:
        print(f"✗ Error: {e}")
        return False

if __name__ == "__main__":
    try:
        record_wake_word()
    except KeyboardInterrupt:
        print("\n\nCancelled by user.")
        sys.exit(1)
