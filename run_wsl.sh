#!/bin/bash

echo "=== WSL GUI Application Launcher ==="

# Check for DISPLAY environment variable
if [ -z "$DISPLAY" ]; then
    echo "DISPLAY environment variable not set"
    echo "Setting DISPLAY=:0 for X server compatibility"
    export DISPLAY=:0
fi

# Check if X server is accessible
if command -v xset &> /dev/null; then
    if ! xset q &>/dev/null; then
        echo "⚠️  WARNING: Unable to connect to X server at $DISPLAY"
        echo "Please ensure X server (VcXsrv, X410, etc) is running on Windows"
        echo "   and Windows Firewall allows WSL connections"
        
        # Check for WSLg
        if [ -d "/mnt/wslg" ]; then
            echo "✓ WSLg detected - will try using Wayland"
            export QT_QPA_PLATFORM=wayland
        else
            echo "Falling back to offscreen mode (no GUI)"
            export QT_QPA_PLATFORM=offscreen
        fi
    else
        echo "✓ X server connection successful at DISPLAY=$DISPLAY"
        export QT_QPA_PLATFORM=xcb
    fi
else
    # No xset available
    if [ -d "/mnt/wslg" ]; then
        echo "✓ WSLg detected - will try using Wayland"
        export QT_QPA_PLATFORM=wayland
    else
        echo "X11 tools not found, trying Wayland"
        export QT_QPA_PLATFORM=wayland
    fi
fi

# Output diagnostic information
echo "Using platform: $QT_QPA_PLATFORM"
echo "Display: $DISPLAY"

# Run the application with all passed arguments
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
APP_PATH="$SCRIPT_DIR/build/bin/CFGParser"

if [ ! -f "$APP_PATH" ]; then
    # Try to find the executable
    APP_PATH="$SCRIPT_DIR/CFGParser"
    if [ ! -f "$APP_PATH" ]; then
        # Check build directory
        APP_PATH="$SCRIPT_DIR/build/CFGParser"
        if [ ! -f "$APP_PATH" ]; then
            echo "⚠️  Could not locate CFGParser executable"
            echo "Please specify the full path: ./run_in_wsl.sh /path/to/CFGParser"
            
            if [ $# -gt 0 ]; then
                # Use the provided argument as the path
                APP_PATH="$1"
                shift  # Remove the first argument
            else
                exit 1
            fi
        fi
    fi
fi

echo "Starting: $APP_PATH $@"
"$APP_PATH" "$@"

# Report exit code
EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    echo "Application exited with code: $EXIT_CODE"
fi

exit $EXIT_CODE