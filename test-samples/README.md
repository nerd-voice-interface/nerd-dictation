# Test Samples Directory

This directory contains audio samples for testing the nerd-dictation frontend.

## Recording Samples

To record test samples, run:

```bash
./scripts/record-test-samples.sh
```

The script will guide you through recording all necessary test phrases.

## Sample Categories

### Basic Text (01-03)
- Simple phrases for basic recognition testing

### Punctuation Commands (04-17)
- Phrases containing punctuation commands (точка, запятая, etc.)
- Tests punctuation replacement functionality

### Key Commands (18-21)
- Phrases with key commands (выполнить, выполнять, etc.)
- Tests key command extraction

### Emoji Commands (22-29)
- Phrases with emoji commands (смайлик улыбка, etc.)
- Tests emoji insertion

### Complex Phrases (30-33)
- Mixed content with multiple commands
- Tests complex text processing

### Capitalization Tests (34-38)
- Phrases testing capitalization after punctuation
- Tests capitalization logic

### Mixed Content (39-41)
- Various combinations of text, punctuation, and commands
- Tests overall processing pipeline

## Using Samples for Testing

### Manual Testing
1. Play samples using audio player
2. Use with dummy backend to test text processing
3. Verify output matches expected results

### Automated Testing
Samples can be used in automated tests by:
1. Playing samples through audio system
2. Capturing audio output from daemon
3. Comparing processed text with expected results

## File Format

- Format: WAV
- Sample rate: 16000 Hz
- Channels: 1 (mono)
- Bit depth: 16-bit

## Notes

- Samples are recorded with your voice for realistic testing
- Dummy backend generates random phrases, so exact matching may not be possible
- Focus on testing text processing logic rather than recognition accuracy
- Samples are git-ignored (not committed to repository)
