#!/bin/bash
# Windows build configuration

echo "Building for Windows..."

if command -v xmake &> /dev/null; then
    xmake repo -u || echo "Failed to update repos"
    xmake f -y -p windows -a x64 -m release || exit 1
    xmake -y || exit 1
    echo "Build complete!"
    ls -la bin/ 2>/dev/null
else
    echo "ERROR: xmake not found"
    echo "Please install xmake first: bash setup.sh"
    exit 1
fi 