# PowerShell script to build LeviLamina mod on Windows
# Run this on a Windows machine with Visual Studio 2022 installed

Write-Host "Building LeviLamina Mod for Windows..." -ForegroundColor Green
Write-Host ""

# Check if xmake is installed
$xmake = Get-Command xmake -ErrorAction SilentlyContinue
if (-not $xmake) {
    Write-Host "ERROR: xmake not found" -ForegroundColor Red
    Write-Host "Please install xmake from: https://xmake.io/" -ForegroundColor Yellow
    exit 1
}

# Check if Visual Studio is installed
$vsVersion = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $vsVersion) {
    Write-Host "ERROR: Visual Studio C++ compiler (cl.exe) not found" -ForegroundColor Red
    Write-Host "Please install Visual Studio 2022 with C++ tools" -ForegroundColor Yellow
    exit 1
}

Write-Host "Found xmake: $(xmake --version)"
Write-Host "Found MSVC compiler"
Write-Host ""

# Clean previous builds
Write-Host "Cleaning previous builds..." -ForegroundColor Cyan
xmake clean

# Configure for Windows x86_64 Release
Write-Host "Configuring build..." -ForegroundColor Cyan
xmake f -p windows -a x86_64 --target_type=server -m release

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Configuration failed" -ForegroundColor Red
    exit 1
}

# Build
Write-Host "Building..." -ForegroundColor Cyan
xmake

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host "Output DLL location: build\windows\x86_64\release\" -ForegroundColor Green

# List the built DLL files
if (Test-Path "build\windows\x86_64\release\*.dll") {
    Get-Item "build\windows\x86_64\release\*.dll" | ForEach-Object {
        Write-Host "  - $($_.Name) ($([math]::Round($_.Length / 1MB, 2)) MB)"
    }
} else {
    Write-Host "WARNING: No DLL files found" -ForegroundColor Yellow
}
