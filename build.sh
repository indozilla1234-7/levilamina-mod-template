#!/bin/bash
# LeviLamina Mod Build Script for Linux

echo "Building for Linux..."

if command -v xmake &> /dev/null; then
    echo "Configuring..."
    xmake f -y -p linux -a x64 -m release
    echo ""
    echo "Building..."
    xmake
    echo ""
    echo "Build complete!"
    if [ -d "bin/" ]; then
        echo "Output files:"
        ls -la bin/
    fi
else
    echo "ERROR: xmake not found"
    echo "Please install xmake first: bash setup.sh"
    exit 1
fi
 