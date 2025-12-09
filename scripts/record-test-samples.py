#!/usr/bin/env python3
"""
Simple script to record audio samples for testing nerd-dictation frontend.
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
SAMPLE_RATE = 16000
CHANNELS = 1
DURATION = 5  # seconds

# Test phrases to record
TEST_PHRASES = [
    # Basic Text
    ("01-basic-hello.wav", "привет мир"),
    ("02-basic-test.wav", "это тестовый текст"),
    ("03-basic-recognition.wav", "распознавание речи работает"),
    
    # Punctuation Commands
    ("04-punct-period.wav", "привет точка мир"),
    ("05-punct-comma.wav", "текст запятая продолжение"),
    ("06-punct-colon.wav", "список двоеточие первый второй"),
    ("07-punct-exclamation.wav", "восклицательный знак"),
    ("08-punct-question.wav", "вопросительный знак"),
    ("09-punct-ellipsis.wav", "многоточие"),
    ("10-punct-quotes-full.wav", "открыть кавычки текст закрыть кавычки"),
    ("11-punct-paren-full.wav", "открыть скобку текст закрыть скобку"),
    ("12-punct-newline.wav", "новая строка"),
    ("13-punct-paragraph.wav", "новый абзац"),
    
    # Key Commands
    ("14-key-execute.wav", "выполнить"),
    ("15-key-execute-variant1.wav", "выполнять"),
    ("16-key-execute-variant2.wav", "выполняй"),
    ("17-key-execute-variant3.wav", "выполни"),
    
    # Emoji Commands
    ("18-emoji-smile.wav", "смайлик улыбка"),
    ("19-emoji-laugh.wav", "смайлик смех"),
    ("20-emoji-cool.wav", "смайлик крутой"),
    ("21-emoji-heart.wav", "смайлик сердце"),
    
    # Complex Phrases
    ("22-complex-mixed-punct.wav", "привет запятая как дела точка"),
    ("23-complex-with-key.wav", "начало выполнить конец"),
    ("24-complex-multiple-punct.wav", "список двоеточие первый запятая второй точка"),
    ("25-complex-long.wav", "привет точка это тест запятая проверка работы системы точка"),
    
    # Capitalization Tests
    ("26-capital-after-period.wav", "привет точка мир"),
    ("27-capital-after-exclamation.wav", "текст восклицательный знак новый текст"),
    ("28-capital-after-question.wav", "вопрос вопросительный знак ответ"),
    
    # Mixed Content
    ("29-mixed-text-punct.wav", "привет запятая как дела точка все хорошо"),
    ("30-mixed-text-key.wav", "напиши код выполнить"),
]

def record_sample(filename, phrase):
    """Record a single audio sample."""
    filepath = os.path.join(SAMPLES_DIR, filename)
    
    print(f"\n{'='*50}")
    print(f"Sample: {filename}")
    print(f"Phrase to say: \"{phrase}\"")
    print(f"{'='*50}")
    
    try:
        input("Press ENTER when ready to record (or Ctrl+C to skip)...")
    except KeyboardInterrupt:
        print("\nSkipped.")
        return False
    
    print(f"\nRecording... ({DURATION} seconds)")
    print("Speak now!")
    
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
        print(f"✓ Saved: {filename} ({file_size:,} bytes)")
        return True
        
    except KeyboardInterrupt:
        print("\nRecording interrupted.")
        return False
    except Exception as e:
        print(f"✗ Error: {e}")
        return False

def main():
    """Main function."""
    # Create samples directory
    os.makedirs(SAMPLES_DIR, exist_ok=True)
    
    print("="*50)
    print("Recording Test Samples for nerd-dictation")
    print("="*50)
    print(f"\nSamples directory: {os.path.abspath(SAMPLES_DIR)}")
    print(f"Sample rate: {SAMPLE_RATE} Hz")
    print(f"Channels: {CHANNELS}")
    print(f"Duration per sample: {DURATION} seconds")
    print(f"\nTotal samples to record: {len(TEST_PHRASES)}")
    print("\nInstructions:")
    print("1. Press ENTER when ready to record each phrase")
    print("2. Speak the phrase clearly")
    print("3. Recording will stop automatically after 5 seconds")
    print("4. Press Ctrl+C to skip a sample")
    
    try:
        input("\nPress ENTER to start recording samples...")
    except KeyboardInterrupt:
        print("\nCancelled.")
        return
    
    recorded = 0
    skipped = 0
    
    for filename, phrase in TEST_PHRASES:
        if record_sample(filename, phrase):
            recorded += 1
        else:
            skipped += 1
    
    print("\n" + "="*50)
    print("Recording Complete!")
    print("="*50)
    print(f"\nSamples recorded: {recorded}")
    print(f"Samples skipped: {skipped}")
    print(f"Total: {len(TEST_PHRASES)}")
    print(f"\nSamples saved to: {os.path.abspath(SAMPLES_DIR)}")
    
    if recorded > 0:
        print("\nSample files:")
        for filename, _ in TEST_PHRASES:
            filepath = os.path.join(SAMPLES_DIR, filename)
            if os.path.exists(filepath):
                size = os.path.getsize(filepath)
                print(f"  {filename} ({size:,} bytes)")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\nRecording cancelled by user.")
        sys.exit(1)
