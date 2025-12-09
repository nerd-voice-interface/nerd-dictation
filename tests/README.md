# Automated Tests for nerd-dictation

This directory contains automated tests using GoogleTest framework and the dummy backend.

## Building Tests

1. Initialize submodules (if not already done):
   ```bash
   git submodule update --init --recursive
   ```

2. Configure CMake with tests enabled:
   ```bash
   mkdir build && cd build
   cmake .. -DENABLE_TESTS=ON
   ```

3. Build:
   ```bash
   make -j$(nproc)
   ```

## Running Tests

### Run all tests:
```bash
cd build
ctest --output-on-failure
```

### Run specific test executable:
```bash
cd build
./tests/nerd-dictation-tests
```

### Run with verbose output:
```bash
cd build
./tests/nerd-dictation-tests --gtest_color=yes
```

## Test Categories

### test_backend_wrapper.cpp
Tests for Backend C++ wrapper:
- Loading dummy backend
- Backend information retrieval
- Audio feeding
- Result retrieval
- Streaming recognition
- Backend reset

### test_postprocess.cpp
Tests for text post-processing:
- Punctuation replacement (точка, запятая, etc.)
- Key commands (выполнить, etc.)
- Emoji commands (смайлик улыбка, etc.)
- Capitalization after punctuation
- Complex phrase processing

### test_input_simulator.cpp
Tests for input simulation:
- stdout method (for testing)
- Key press simulation
- Unicode text handling

### test_config.cpp
Tests for configuration:
- Default config values
- Config structure

### test_daemon.cpp
Tests for daemon functionality:
- Daemon creation
- State management
- Begin/end/cancel dictation
- Multiple session handling

## Test Requirements

- Dummy backend must be built (`libnerd-dictation-dummy.so`)
- Tests automatically find the backend via:
  - `NERD_DICTATION_DUMMY_BACKEND_PATH` environment variable
  - `CMAKE_BINARY_DIR` environment variable
  - Relative paths from test executable
  - System library paths

## Notes

- Tests use dummy backend which generates random phrases
- Focus is on testing frontend logic, not recognition accuracy
- Some tests may be skipped if dummy backend is not found (GTEST_SKIP)
- Tests run automatically via CTest integration
