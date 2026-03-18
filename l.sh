#!/bin/bash
# Linux build configuration

echo "Building for Linux..."

if command -v xmake &> /dev/null; then
    xmake f -y -p linux -a x64 -m release
    xmake
    echo "Build complete!"
    [ -d "bin/" ] && ls -la bin/
else
    echo "ERROR: xmake not found"
    echo "Please install xmake first: bash setup.sh"
    exit 1
fi
