#!/bin/bash
# Setup script - Install xmake on Linux

echo "Installing xmake for Linux..."

# Method 1: Using package manager (most reliable)
if command -v apt &> /dev/null; then
    echo "Installing via apt..."
    sudo apt-get update
    sudo apt-get install -y xmake
elif command -v brew &> /dev/null; then
    echo "Installing via brew..."
    brew install xmake
else
    # Method 2: Direct download (requires curl)
    if command -v curl &> /dev/null; then
        echo "Installing via xmake installer..."
        bash <(curl -fsSL https://xmake.io/shget.text)
        export PATH="$HOME/.local/bin:$PATH"
    else
        echo "ERROR: No package manager or curl found"
        echo "Please install xmake manually: https://xmake.io/#/guide/installation"
        exit 1
    fi
fi

echo "Verifying xmake installation..."
if xmake --version; then
    echo "xmake installed successfully!"
else
    echo "ERROR: xmake installation failed"
    exit 1
fi
