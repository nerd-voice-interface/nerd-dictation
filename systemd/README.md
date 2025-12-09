# Systemd service for nerd-dictation

## Installation

The service file is automatically installed when running `make install`:

```bash
cd build
cmake ..
make install
```

The service file will be installed to `${CMAKE_INSTALL_PREFIX}/lib/systemd/user/nerd-dictation.service`

## Usage

### Enable Autostart

```bash
systemctl --user daemon-reload
systemctl --user enable nerd-dictation
systemctl --user start nerd-dictation
```

### Management

```bash
# Start
systemctl --user start nerd-dictation

# Stop
systemctl --user stop nerd-dictation

# Status
systemctl --user status nerd-dictation

# Logs
journalctl --user -u nerd-dictation -f
```

### Disable Autostart

```bash
systemctl --user disable nerd-dictation
```

## Configuration

The service file uses the `@CMAKE_INSTALL_PREFIX@` variable, which is substituted during build via `configure_file`.

Executable path in the service file:
```
ExecStart=@CMAKE_INSTALL_PREFIX@/bin/nerd-dictation daemon
```

After installation, it will be replaced with the actual path, for example:
```
ExecStart=/home/user/forge/nerd-dictation/install/bin/nerd-dictation daemon
```

## Features

- **User service**: Installed to `lib/systemd/user/` (does not require root)
- **Dependencies**: Requires PipeWire/PulseAudio
- **Security**: Restrictions enabled via systemd security features
- **Resources**: Memory limit up to 2GB, CPU up to 200%

## Manual Installation (without CMake)

If you need to install the service file manually:

```bash
# Edit the path in the service file
sed "s|@CMAKE_INSTALL_PREFIX@|/path/to/install|g" \
    systemd/nerd-dictation.service.in > \
    ~/.config/systemd/user/nerd-dictation.service

# Reload systemd
systemctl --user daemon-reload
```
