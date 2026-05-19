<#
.SYNOPSIS
    Build deskflow installer (EXE/MSI) on Windows.

.DESCRIPTION
    Installs all required tools and builds deskflow for Windows.
    Produces a WiX MSI installer and a 7-Zip archive in the build/ directory.

.PARAMETER QtVersion
    Qt version to install (default: 6.10.2)

.PARAMETER Arch
    Target architecture: x64 or arm64 (default: x64)

.PARAMETER SkipToolInstall
    Skip installing Scoop / WiX (use if already installed)

.EXAMPLE
    .\scripts\build-windows.ps1
    .\scripts\build-windows.ps1 -Arch arm64
    .\scripts\build-windows.ps1 -SkipToolInstall
#>

param(
    [string]$QtVersion = "6.10.2",
    [ValidateSet("x64","arm64")]
    [string]$Arch = "x64",
    [switch]$SkipToolInstall
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root

Write-Host "=== Deskflow Windows Build Script ===" -ForegroundColor Cyan
Write-Host "Architecture: $Arch"
Write-Host "Qt version:   $QtVersion"

# ─── 1. Install tools via Scoop / winget ────────────────────────────────────
if (-not $SkipToolInstall) {
    Write-Host "`n[1/6] Installing build tools..." -ForegroundColor Yellow

    # Install Scoop if not present
    if (-not (Get-Command scoop -ErrorAction SilentlyContinue)) {
        Write-Host "Installing Scoop..."
        Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force
        Invoke-RestMethod -Uri https://get.scoop.sh | Invoke-Expression
    }

    scoop install cmake ninja git

    # Install .NET (required for WiX 4)
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
        winget install Microsoft.DotNet.SDK.8 --accept-package-agreements --silent
        $env:PATH += ";$env:LOCALAPPDATA\Microsoft\dotnet"
    }

    # Install WiX 4 via dotnet tool
    if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
        Write-Host "Installing WiX 4..."
        dotnet tool install --global wix --version 4.0.5
        # Add dotnet tools to PATH for this session
        $env:PATH += ";$env:USERPROFILE\.dotnet\tools"
    }

    Write-Host "Build tools ready." -ForegroundColor Green
}

# ─── 2. Install Qt via aqt ──────────────────────────────────────────────────
Write-Host "`n[2/6] Checking Qt $QtVersion..." -ForegroundColor Yellow

$QtBase = "C:\Qt"
$QtDir  = "$QtBase\$QtVersion\msvc2022_64"
if (-not (Test-Path $QtDir)) {
    if (-not (Get-Command pip -ErrorAction SilentlyContinue)) {
        Write-Error "pip not found. Please install Python from https://python.org"
    }
    pip install aqtinstall --quiet
    $target = if ($Arch -eq "arm64") { "win64_msvc2022_arm64" } else { "win64_msvc2022_64" }
    python -m aqt install-qt windows desktop $QtVersion $target -O $QtBase
    $QtDir = "$QtBase\$QtVersion\$target"
}
$env:Qt6_DIR = $QtDir
$env:PATH = "$QtDir\bin;$env:PATH"
Write-Host "Qt dir: $QtDir" -ForegroundColor Green

# ─── 3. Install vcpkg dependencies ──────────────────────────────────────────
Write-Host "`n[3/6] Building vcpkg dependencies (OpenSSL, gtest)..." -ForegroundColor Yellow

$VcpkgRoot = "C:\vcpkg"
if (-not (Test-Path $VcpkgRoot)) {
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    & "$VcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
}

$triplet = if ($Arch -eq "arm64") { "arm64-windows" } else { "x64-windows-release" }
& "$VcpkgRoot\vcpkg.exe" install "gtest:$triplet" "openssl:$triplet" --host-triplet=$triplet

$VcpkgCmake = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
Write-Host "vcpkg ready." -ForegroundColor Green

# ─── 4. Configure ───────────────────────────────────────────────────────────
Write-Host "`n[4/6] Configuring CMake..." -ForegroundColor Yellow

$Version = "1.0.0-relay"
cmake -Bbuild `
      -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DSKIP_BUILD_TESTS=ON `
      "-DCMAKE_TOOLCHAIN_FILE=$VcpkgCmake" `
      "-DVCPKG_TARGET_TRIPLET=$triplet" `
      "-DPACKAGE_VERSION_LABEL=$Version"

# ─── 5. Build & Package ─────────────────────────────────────────────────────
Write-Host "`n[5/6] Building and packaging..." -ForegroundColor Yellow
cmake --build build --config Release --target package -j $env:NUMBER_OF_PROCESSORS

# ─── 6. Show output ─────────────────────────────────────────────────────────
Write-Host "`n[6/6] Packages created:" -ForegroundColor Green
Get-ChildItem "build\deskflow*" | ForEach-Object { Write-Host "  $_" -ForegroundColor White }

Write-Host "`nDone! Packages are in the build\ directory." -ForegroundColor Cyan
