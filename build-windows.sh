#!/bin/bash
# Build LeviLamina mod using Windows Docker container

echo "Building LeviLamina mod for Windows using Docker..."
echo ""
echo "Note: This requires Docker Desktop with Windows containers enabled!"
echo "If you're on a Mac or Linux, you cannot run Windows containers."
echo ""

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "ERROR: Docker is not installed"
    echo "Please install Docker Desktop from https://www.docker.com/products/docker-desktop"
    exit 1
fi

# Build the Windows Docker image
echo "Building Windows Docker image..."
docker build -f Dockerfile.windows -t levilamina-mod-builder:windows .

if [ $? -ne 0 ]; then
    echo "ERROR: Docker build failed"
    echo "Make sure you have Windows containers enabled in Docker Desktop"
    exit 1
fi

# Run the container and extract the output
echo "Running build in Windows container..."
docker run --rm -v $(pwd)/build:/workspace/build levilamina-mod-builder:windows

echo ""
echo "Build complete! Windows DLL should be in: build/windows/x86_64/release/"
ls -la build/windows/x86_64/release/*.dll 2>/dev/null || echo "DLL not found - check build output above"
