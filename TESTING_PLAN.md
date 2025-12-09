# Testing Plan for nerd-dictation Frontend

## Overview
This document outlines a comprehensive testing plan for the nerd-dictation frontend using the dummy backend. The dummy backend generates random phrases, making it suitable for testing the frontend's text processing, input simulation, and command handling functionality.

## Test Environment Setup

### Prerequisites
- Built nerd-dictation binary
- Dummy backend compiled and available
- Audio capture working (PipeWire/PulseAudio)
- Input simulation method available (xdotool/ydotool/wtype/dotool/stdout)

### Configuration
- Use dummy backend: `backend = "dummy"` in config
- Set input method: `input_method = "stdout"` for testing (or actual method for integration tests)

## Test Categories

### 1. CLI Commands Testing

#### 1.1 Daemon Management
- [ ] **Test**: Start daemon
  - Command: `nerd-dictation daemon`
  - Expected: Daemon starts, socket created, audio listener active
  - Verify: Check socket exists, logs show initialization

- [ ] **Test**: Check daemon status
  - Command: `nerd-dictation toggle` (when daemon not running)
  - Expected: Error message about daemon not running

- [ ] **Test**: Daemon responds to status query
  - Command: Send `{"action":"status"}` via socket
  - Expected: JSON response with state and backend name

#### 1.2 Dictation Control
- [ ] **Test**: Begin dictation
  - Command: `nerd-dictation begin`
  - Expected: Dictation starts, begin sound plays, state changes to "listening"
  - Verify: Check logs, verify sound playback

- [ ] **Test**: End dictation
  - Command: `nerd-dictation end`
  - Expected: Dictation stops, end sound plays, state changes to "idle"
  - Verify: Check logs, verify sound playback

- [ ] **Test**: Cancel dictation
  - Command: `nerd-dictation cancel`
  - Expected: Dictation cancelled, backend reset, no end sound
  - Verify: Check logs, verify backend reset

- [ ] **Test**: Toggle dictation
  - Command: `nerd-dictation toggle` (multiple times)
  - Expected: Alternates between begin/end based on current state
  - Verify: Check state transitions

- [ ] **Test**: Multiple begin commands
  - Command: `nerd-dictation begin` (twice)
  - Expected: Warning on second call, no duplicate threads

- [ ] **Test**: End when not listening
  - Command: `nerd-dictation end` (when idle)
  - Expected: Warning message, no error

### 2. Text Processing Testing

#### 2.1 Punctuation Replacement
Test phrases with punctuation commands (record audio samples):

- [ ] **Test**: "привет точка мир"
  - Expected output: "привет. мир"
  - Verify: Space removed before period

- [ ] **Test**: "текст запятая продолжение"
  - Expected output: "текст, продолжение"
  - Verify: Space removed before comma

- [ ] **Test**: "список двоеточие первый второй"
  - Expected output: "список: первый второй"
  - Verify: Space removed before colon

- [ ] **Test**: "восклицательный знак"
  - Expected output: "!"
  - Verify: Word replaced with punctuation

- [ ] **Test**: "вопросительный знак"
  - Expected output: "?"
  - Verify: Word replaced with punctuation

- [ ] **Test**: "многоточие"
  - Expected output: "..."
  - Verify: Word replaced with ellipsis

- [ ] **Test**: "открыть кавычки текст закрыть кавычки"
  - Expected output: "«текст»"
  - Verify: Quotes properly placed

- [ ] **Test**: "открыть скобку текст закрыть скобку"
  - Expected output: "(текст)"
  - Verify: Parentheses properly placed

- [ ] **Test**: "новая строка"
  - Expected output: "\n"
  - Verify: Line break inserted

- [ ] **Test**: "новый абзац"
  - Expected output: "\n\n"
  - Verify: Double line break inserted

#### 2.2 Capitalization
- [ ] **Test**: "привет точка мир"
  - Expected output: "привет. Мир"
  - Verify: Capitalization after period

- [ ] **Test**: "текст восклицательный знак новый текст"
  - Expected output: "текст! Новый текст"
  - Verify: Capitalization after exclamation

- [ ] **Test**: "вопрос вопросительный знак ответ"
  - Expected output: "вопрос? Ответ"
  - Verify: Capitalization after question mark

- [ ] **Test**: "первая строка новая строка вторая строка"
  - Expected output: "первая строка\nВторая строка"
  - Verify: Capitalization after newline

#### 2.3 Key Commands
- [ ] **Test**: "выполнить"
  - Expected: KeyAction with "ctrl+Return"
  - Verify: Input simulator receives key press command

- [ ] **Test**: "выполнять"
  - Expected: KeyAction with "ctrl+Return"
  - Verify: Variant form recognized

- [ ] **Test**: "выполняй"
  - Expected: KeyAction with "ctrl+Return"
  - Verify: Variant form recognized

- [ ] **Test**: "выполни"
  - Expected: KeyAction with "ctrl+Return"
  - Verify: Variant form recognized

#### 2.4 Emoji Commands
- [ ] **Test**: "смайлик улыбка"
  - Expected output: "😊"
  - Verify: Emoji inserted

- [ ] **Test**: "смайлик смех"
  - Expected output: "😂"
  - Verify: Emoji inserted

- [ ] **Test**: "смайлик крутой"
  - Expected output: "😎"
  - Verify: Emoji inserted

- [ ] **Test**: "смайлик сердце"
  - Expected output: "❤️"
  - Verify: Emoji inserted

- [ ] **Test**: Capitalization after emoji
  - Input: "привет смайлик улыбка мир"
  - Expected output: "привет😊 Мир"
  - Verify: Capitalization works with emojis

#### 2.5 Complex Text Processing
- [ ] **Test**: Mixed punctuation and text
  - Input: "привет запятая как дела точка"
  - Expected output: "привет, как дела."
  - Verify: Multiple punctuation marks processed correctly

- [ ] **Test**: Key command in middle of text
  - Input: "начало выполнить конец"
  - Expected: TextAction("начало "), KeyAction("ctrl+Return"), TextAction(" конец")
  - Verify: Commands extracted correctly

- [ ] **Test**: Multiple punctuation marks
  - Input: "список двоеточие первый запятая второй точка"
  - Expected output: "список: первый, второй."
  - Verify: All punctuation processed

- [ ] **Test**: Long text with various commands
  - Input: "привет точка это тест запятая проверка работы системы точка"
  - Expected output: "Привет. Это тест, проверка работы системы."
  - Verify: Complex processing works

### 3. Input Simulation Testing

#### 3.1 stdout Method (for testing)
- [ ] **Test**: Text output to stdout
  - Input: "привет мир"
  - Expected: Text printed to stdout
  - Verify: Check stdout output

- [ ] **Test**: Key command output
  - Input: "выполнить"
  - Expected: "[KEY:ctrl+Return]" printed to stdout
  - Verify: Check stdout output

#### 3.2 Actual Input Methods (integration tests)
- [ ] **Test**: xdotool method
  - Configuration: `input_method = "xdotool"`
  - Input: "тест"
  - Expected: Text typed in active window
  - Verify: Check active application

- [ ] **Test**: ydotool method
  - Configuration: `input_method = "ydotool"`
  - Input: "тест"
  - Expected: Text typed in active window
  - Verify: Check active application

- [ ] **Test**: wtype method
  - Configuration: `input_method = "wtype"`
  - Input: "тест"
  - Expected: Text typed in active window
  - Verify: Check active application

- [ ] **Test**: dotool method
  - Configuration: `input_method = "dotool"`
  - Input: "тест"
  - Expected: Text typed in active window
  - Verify: Check active application

### 4. Audio Processing Testing

#### 4.1 Audio Capture
- [ ] **Test**: Audio capture starts
  - Action: Start daemon
  - Expected: Audio listener thread starts, audio process launched
  - Verify: Check logs, verify process exists

- [ ] **Test**: Audio chunks received
  - Action: Speak during dictation
  - Expected: Audio chunks added to buffer
  - Verify: Check logs for audio processing

#### 4.2 Audio Preprocessing (if enabled)
- [ ] **Test**: AGC enabled
  - Configuration: Enable AGC in config
  - Expected: Audio preprocessing active
  - Verify: Check logs

- [ ] **Test**: Noise suppression enabled
  - Configuration: Enable noise suppression
  - Expected: Audio preprocessing active
  - Verify: Check logs

### 5. Sound System Testing

#### 5.1 Sound Playback
- [ ] **Test**: Begin sound plays
  - Action: Start dictation
  - Expected: Begin sound plays
  - Verify: Hear sound, check logs

- [ ] **Test**: End sound plays
  - Action: End dictation
  - Expected: End sound plays
  - Verify: Hear sound, check logs

- [ ] **Test**: Execute sound plays
  - Action: Issue key command
  - Expected: Execute sound plays
  - Verify: Hear sound, check logs

#### 5.2 Sound Loading
- [ ] **Test**: Embedded sounds load
  - Expected: begin.wav, end.wav, execute.wav loaded
  - Verify: Check logs for sound loading

- [ ] **Test**: Sound system initialization
  - Expected: Sound subsystem initialized
  - Verify: Check logs

### 6. Backend Integration Testing

#### 6.1 Dummy Backend
- [ ] **Test**: Backend loads
  - Expected: Dummy backend loaded successfully
  - Verify: Check logs for backend loading

- [ ] **Test**: Backend info retrieved
  - Expected: Backend name "dummy", sample rate 16000 Hz
  - Verify: Check logs

- [ ] **Test**: Streaming recognition works
  - Action: Start dictation, provide audio
  - Expected: Recognition results generated
  - Verify: Check logs for recognition results

- [ ] **Test**: Partial results
  - Expected: Partial results generated during recognition
  - Verify: Check logs

- [ ] **Test**: Final results
  - Expected: Final results generated
  - Verify: Check logs, verify text processing

- [ ] **Test**: Backend reset
  - Action: Cancel dictation
  - Expected: Backend reset called
  - Verify: Check logs

### 7. Error Handling Testing

#### 7.1 Daemon Errors
- [ ] **Test**: Daemon not running error
  - Command: `nerd-dictation begin` (without daemon)
  - Expected: Error message
  - Verify: Check error output

- [ ] **Test**: Socket connection error
  - Action: Remove socket, try to connect
  - Expected: Error handling
  - Verify: Check error messages

#### 7.2 Audio Errors
- [ ] **Test**: Audio capture failure
  - Action: Kill audio process
  - Expected: Error logged, reconnection attempted
  - Verify: Check logs

- [ ] **Test**: Invalid audio format
  - Expected: Error handling
  - Verify: Check logs

#### 7.3 Backend Errors
- [ ] **Test**: Backend load failure
  - Action: Use invalid backend path
  - Expected: Error message, graceful failure
  - Verify: Check logs

- [ ] **Test**: Backend recognition error
  - Expected: Error handling
  - Verify: Check logs

### 8. State Management Testing

#### 8.1 State Transitions
- [ ] **Test**: Idle → Listening
  - Action: Begin dictation
  - Expected: State changes to Listening
  - Verify: Check status response

- [ ] **Test**: Listening → Idle
  - Action: End dictation
  - Expected: State changes to Idle
  - Verify: Check status response

- [ ] **Test**: Listening → Idle (cancel)
  - Action: Cancel dictation
  - Expected: State changes to Idle
  - Verify: Check status response

#### 8.2 Concurrent Operations
- [ ] **Test**: Multiple begin commands
  - Action: Send multiple begin commands
  - Expected: Only one dictation active
  - Verify: Check state

- [ ] **Test**: Rapid toggle
  - Action: Rapidly toggle dictation
  - Expected: State changes correctly
  - Verify: Check state transitions

### 9. Performance Testing

#### 9.1 Latency
- [ ] **Test**: Recognition latency
  - Measure: Time from audio input to text output
  - Expected: < 2 seconds for dummy backend
  - Verify: Measure timing

#### 9.2 Throughput
- [ ] **Test**: Continuous recognition
  - Action: Long dictation session
  - Expected: No memory leaks, stable performance
  - Verify: Monitor memory, check logs

### 10. Integration Testing

#### 10.1 Full Workflow
- [ ] **Test**: Complete dictation session
  1. Start daemon
  2. Begin dictation
  3. Speak text with commands
  4. Verify text processing
  5. Issue key command
  6. End dictation
  7. Verify all steps completed

#### 10.2 Multiple Sessions
- [ ] **Test**: Multiple dictation sessions
  - Action: Start/stop dictation multiple times
  - Expected: Each session works independently
  - Verify: Check state, verify processing

## Test Data Requirements

### Audio Samples Needed
For comprehensive testing, record audio samples for:

1. **Basic Text**
   - "привет мир"
   - "это тестовый текст"
   - "распознавание речи работает"

2. **Punctuation Commands**
   - "привет точка мир"
   - "текст запятая продолжение"
   - "список двоеточие первый второй"
   - "восклицательный знак"
   - "вопросительный знак"
   - "многоточие"
   - "открыть кавычки текст закрыть кавычки"
   - "открыть скобку текст закрыть скобку"
   - "новая строка"
   - "новый абзац"

3. **Key Commands**
   - "выполнить"
   - "выполнять"
   - "выполняй"
   - "выполни"

4. **Emoji Commands**
   - "смайлик улыбка"
   - "смайлик смех"
   - "смайлик крутой"
   - "смайлик сердце"

5. **Complex Phrases**
   - "привет запятая как дела точка"
   - "начало выполнить конец"
   - "список двоеточие первый запятая второй точка"
   - "привет точка это тест запятая проверка работы системы точка"

## Test Execution Order

1. **Setup Phase**
   - Build project
   - Configure dummy backend
   - Prepare test environment

2. **Unit Tests**
   - Text processing functions
   - Input simulation
   - Command parsing

3. **Integration Tests**
   - Daemon startup/shutdown
   - Dictation control
   - Audio processing

4. **End-to-End Tests**
   - Full workflow
   - Multiple sessions
   - Error scenarios

5. **Performance Tests**
   - Latency measurements
   - Throughput tests
   - Memory leak checks

## Success Criteria

- All CLI commands work correctly
- Text processing handles all punctuation and commands
- Input simulation works for all methods
- Sound system plays all sounds
- Error handling is graceful
- State management is correct
- Performance is acceptable (< 2s latency)
- No memory leaks or crashes

## Notes

- Dummy backend generates random phrases, so exact text matching may not be possible
- Focus on testing text processing logic rather than recognition accuracy
- Use stdout input method for easier testing
- Monitor logs for detailed debugging information
