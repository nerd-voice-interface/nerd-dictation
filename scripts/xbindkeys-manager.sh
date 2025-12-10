#!/bin/bash
# Script to manage xbindkeys service
# Ensures xbindkeys is running and restarts it if needed

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Check if xbindkeys is installed
if ! command -v xbindkeys &> /dev/null; then
    echo "Error: xbindkeys is not installed" >&2
    echo "Install it with: sudo pacman -S xbindkeys" >&2
    exit 1
fi

# Check if config file exists
XBINDKEYS_CONFIG="${HOME}/.xbindkeysrc"
if [ ! -f "$XBINDKEYS_CONFIG" ]; then
    echo "Warning: xbindkeys config not found at $XBINDKEYS_CONFIG" >&2
    echo "Create it first with the touchpad binding configuration" >&2
    exit 1
fi

# Function to check if xbindkeys is running
is_xbindkeys_running() {
    pgrep -x xbindkeys > /dev/null 2>&1
}

# Function to start xbindkeys
start_xbindkeys() {
    if is_xbindkeys_running; then
        echo "xbindkeys is already running (PID: $(pgrep -x xbindkeys))"
        return 0
    fi
    
    echo "Starting xbindkeys..."
    xbindkeys > /dev/null 2>&1 &
    
    # Wait a moment and check if it started successfully
    sleep 0.5
    if is_xbindkeys_running; then
        echo "xbindkeys started successfully (PID: $(pgrep -x xbindkeys))"
        return 0
    else
        echo "Error: Failed to start xbindkeys" >&2
        return 1
    fi
}

# Function to restart xbindkeys
restart_xbindkeys() {
    if is_xbindkeys_running; then
        echo "Stopping xbindkeys..."
        pkill -x xbindkeys || true
        sleep 0.3
    fi
    
    start_xbindkeys
}

# Function to stop xbindkeys
stop_xbindkeys() {
    if ! is_xbindkeys_running; then
        echo "xbindkeys is not running"
        return 0
    fi
    
    echo "Stopping xbindkeys..."
    pkill -x xbindkeys
    sleep 0.3
    
    if is_xbindkeys_running; then
        echo "Warning: xbindkeys is still running, forcing kill..." >&2
        pkill -9 -x xbindkeys || true
    else
        echo "xbindkeys stopped successfully"
    fi
}

# Function to show status
show_status() {
    if is_xbindkeys_running; then
        PID=$(pgrep -x xbindkeys)
        echo "xbindkeys is running (PID: $PID)"
        return 0
    else
        echo "xbindkeys is not running"
        return 1
    fi
}

# Main logic
case "${1:-start}" in
    start)
        start_xbindkeys
        ;;
    stop)
        stop_xbindkeys
        ;;
    restart)
        restart_xbindkeys
        ;;
    status)
        show_status
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}" >&2
        echo "" >&2
        echo "Commands:" >&2
        echo "  start    - Start xbindkeys if not running" >&2
        echo "  stop     - Stop xbindkeys if running" >&2
        echo "  restart  - Restart xbindkeys (stop and start)" >&2
        echo "  status   - Show xbindkeys status" >&2
        exit 1
        ;;
esac
