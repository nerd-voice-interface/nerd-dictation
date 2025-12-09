# nerd-dictation (C++)

C++ implementation of a speech dictation system for Linux with modular architecture and pluggable speech recognition backends.

## Project Status

🚧 **In Development** — The project is in the planning and initial implementation stage.

## Quick Start

### Prerequisites

- CMake 3.20+
- C++20 compiler (GCC 10+, Clang 12+)
- System libraries: PipeWire, PulseAudio

### Installing Dependencies (Arch Linux)

```bash
# System packages
sudo pacman -S pipewire-pulse libpipewire libpulse cmake pkg-config

# Vosk (AUR)
yay -S vosk-api-bin

# ONNX Runtime (optional, for GigaAM)
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

# Initialize all submodules
git submodule update --init --recursive
```

### Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Running

```bash
./nerd-dictation --help
```

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
│   └── main.cpp
└── tests/                  # Tests (optional)
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
