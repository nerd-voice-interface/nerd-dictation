# nerd-dictation (C++)

C++ implementation of a speech dictation system for Linux with modular architecture and pluggable speech recognition backends.

## Project Status

🚧 **In Development** — Core functionality implemented with daemon mode, backend wrapper, text processing, and automated tests.

## Features

- **Daemon mode**: Backend stays loaded in memory for instant response
- **Pluggable backends**: Load ASR backends dynamically via shared libraries (.so)
- **Text post-processing**: Punctuation replacement, key commands, capitalization
- **Wake word detection**: Optional Vosk-based wake word activation
- **Audio preprocessing**: Noise suppression, AGC, resampling
- **Input simulation**: Support for xdotool, ydotool, and stdout (testing)
- **Systemd integration**: Auto-start daemon on login
- **Automated testing**: GoogleTest-based test suite with dummy backend

## Quick Start

### Prerequisites

- CMake 3.20+
- C++20 compiler (GCC 10+, Clang 12+)
- System libraries: PipeWire, PulseAudio

### Installing Dependencies (Arch Linux)

```bash
# System packages
sudo pacman -S pipewire-pulse libpipewire libpulse cmake pkg-config

# Input simulation (choose based on your environment)
sudo pacman -S xdotool      # For X11
yay -S ydotool              # For Wayland (AUR)

# Button binding (for touchpad/mouse button)
sudo pacman -S xbindkeys    # For monitoring button presses

# Vosk (AUR, optional, for wake word detection)
yay -S vosk-api-bin

# ONNX Runtime (optional, for GigaAM backend)
yay -S onnxruntime
```

### Initializing Git Submodules

```bash
# Clone header-only libraries into ThirdParty
git submodule add https://github.com/ToruNiina/toml11.git ThirdParty/toml11
git submodule add https://github.com/gabime/spdlog.git ThirdParty/spdlog
git submodule add https://github.com/CLIUtils/CLI11.git ThirdParty/CLI11
git submodule add https://gitlab.com/libeigen/eigen.git ThirdParty/eigen
git submodule add https://github.com/nerd-voice-interface/nerd-dictation-api.git ThirdParty/nerd-dictation-api
git submodule add https://github.com/google/googletest.git ThirdParty/googletest  # For tests

# Initialize all submodules
git submodule update --init --recursive
```

### Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)

# Build with tests (optional)
cmake .. -DENABLE_TESTS=ON
make -j$(nproc)
```

### Running

```bash
./nerd-dictation --help
```

## Usage

### Recommended Workflow (Daemon Mode)

The daemon mode keeps the backend loaded in memory for instant response:

```bash
# 1. Start daemon (backend loads into memory)
./nerd-dictation daemon

# 2. Control dictation (instant response!)
./nerd-dictation begin    # Start dictation
./nerd-dictation end      # End dictation
./nerd-dictation cancel   # Cancel dictation
./nerd-dictation toggle   # Toggle dictation (begin/end)

# 3. Check daemon status
# Send status command via socket or check systemd service
```

### Systemd Integration (Auto-start)

For automatic daemon startup on login:

```bash
# Install systemd service
cp systemd/nerd-dictation.service.in ~/.config/systemd/user/nerd-dictation.service
# Edit the service file to set correct paths
systemctl --user daemon-reload
systemctl --user enable --now nerd-dictation

# Now daemon starts automatically on login
```

See `systemd/README.md` for detailed systemd setup instructions.

### Key Bindings

#### Option 1: Keyboard (i3/Sway/Hyprland)

For i3/Sway (`~/.config/i3/config` or `~/.config/sway/config`):

```
bindsym $mod+Shift+grave exec /path/to/nerd-dictation toggle
```

For Hyprland (`~/.config/hypr/hyprland.conf`):

```
bind = $mod SHIFT, grave, exec, /path/to/nerd-dictation toggle
```

#### Option 2: Touchpad/Mouse Button (xbindkeys)

To bind to the middle button of mouse or touchpad:

```bash
# Install xbindkeys
sudo pacman -S xbindkeys  # Arch Linux

# Create config
cat > ~/.xbindkeysrc << 'EOF'
"/path/to/nerd-dictation toggle"
  b:2
EOF

# Start xbindkeys
xbindkeys
```

Add autostart to your window manager config:

```
# i3/Sway
exec --no-startup-id xbindkeys

# Hyprland
exec-once = xbindkeys
```

**Button codes:** `b:1` — left, `b:2` — middle (touchpad center button), `b:3` — right.

**Note:** `xbindkeys` is the service used to monitor touchpad/mouse button presses. It works in both X11 and Wayland environments.

### Configuration

Configuration is loaded from TOML files. Create a config file:

```bash
mkdir -p ~/.config/nerd-dictation
cat > ~/.config/nerd-dictation/config.toml << 'EOF'
[general]
backend = "dummy"              # Backend name: "dummy", "t-one", "gigaam", etc.
backend_library = ""           # Path to backend .so file (empty = auto-detect)
input_method = "xdotool"      # Input method: "xdotool", "ydotool", "stdout"

[audio]
source = "pw-cat"             # Audio source: "pw-cat", "parecord", etc.
sample_rate = 16000            # Audio sample rate (Hz)
chunk_size = 8000              # Audio chunk size (samples)

[wake_word]
enabled = true                 # Enable wake word detection
word = "запись"                # Wake word (Russian)
model_path = "~/.local/share/vosk/vosk-model-ru"  # Path to Vosk model
EOF
```

**Configuration options:**

- **Backend selection:**
  - `backend` — Backend name (e.g., "dummy", "t-one", "gigaam")
  - `backend_library` — Path to backend shared library (.so file). If empty, auto-detected based on backend name

- **Input method:**
  - `input_method` — Method for simulating input:
    - `"xdotool"` — X11 input simulation (requires `xdotool`)
    - `"ydotool"` — Wayland input simulation (requires `ydotool`)
    - `"stdout"` — Output to stdout (for testing)

- **Audio settings:**
  - `audio.source` — Audio capture source
  - `audio.sample_rate` — Sample rate in Hz (typically 16000)
  - `audio.chunk_size` — Audio chunk size in samples

- **Wake word detection:**
  - `wake_word.enabled` — Enable/disable wake word detection
  - `wake_word.word` — Wake word phrase (default: "запись")
  - `wake_word.model_path` — Path to Vosk model directory

**Configuration file search order:**
1. `~/.config/nerd-dictation/config.toml`
2. `~/.config/nerd-dictation.toml`
3. System-wide locations (if implemented)

If no config file is found, default values are used.

## Testing

The project includes automated tests using GoogleTest framework:

```bash
# Build with tests enabled
cd build
cmake .. -DENABLE_TESTS=ON
make -j$(nproc)

# Run all tests
ctest --output-on-failure

# Or run test executable directly
./tests/nerd-dictation-tests
```

See `tests/README.md` for detailed testing documentation.

**Test coverage:**
- Backend wrapper (11 tests)
- Text post-processing (26 tests)
- Input simulator (6 tests)
- Configuration (3 tests)
- Daemon functionality (7 tests)
- Space normalization and punctuation spacing (12 tests)

**Total: 74 tests** — all passing ✅

## Project Structure

```
nerd-dictation/
├── CMakeLists.txt          # Main CMake file
├── README.md               # This file
├── ThirdParty/             # Git submodules
│   ├── toml11/            # TOML parser
│   ├── spdlog/            # Logging
│   ├── CLI11/             # CLI parsing
│   ├── eigen/             # Linear algebra
│   └── nerd-dictation-api/ # Backend C API
├── include/                # Public headers
│   └── nerd_dictation/
├── src/                    # Source code
│   ├── main.cpp
│   ├── daemon/            # Daemon implementation
│   ├── audio/             # Audio preprocessing
│   ├── input/             # Input simulation and text processing
│   ├── config/             # Configuration loading
│   ├── sounds/             # Sound notifications
│   └── backends/          # Backend implementations
├── tests/                  # Automated tests (GoogleTest)
│   ├── CMakeLists.txt
│   ├── test_main.cpp
│   ├── test_backend_wrapper.cpp
│   ├── test_postprocess.cpp
│   ├── test_input_simulator.cpp
│   ├── test_config.cpp
│   └── test_daemon.cpp
└── systemd/                # Systemd service files
```

## Dependencies

### System (via pkg-config)
- **libpipewire-0.3** — Audio capture (primary)
- **libpulse** — Audio capture (fallback)
- **libvosk** — Vosk ASR backend (optional)

### Git Submodules (ThirdParty/)
- **toml11** — TOML configuration parsing
- **spdlog** — Logging
- **CLI11** — CLI parsing
- **eigen** — Linear algebra (required, for noise suppression)
- **nerd-dictation-api** — Backend C API

### Optional
- **onnxruntime** — For GigaAM backend

## Build Options

```bash
cmake \
      -DENABLE_GIGAAM_BACKEND=OFF \
      -DENABLE_TESTS=OFF \
      ..
```

## License

GPL-3.0 (same as original nerd-dictation)
