#!/usr/bin/env bash
# Build deskflow .dmg on macOS.
#
# Requirements:
#   - Xcode Command Line Tools:  xcode-select --install
#   - Homebrew:                  https://brew.sh
#
# Usage:
#   chmod +x scripts/build-macos.sh
#   ./scripts/build-macos.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QT_VERSION="${QT_VERSION:-6.10.2}"
ARCH="${ARCH:-$(uname -m)}"  # arm64 or x86_64
VERSION="${1:-1.0.0-relay}"

echo "=== Deskflow macOS Build Script ==="
echo "Qt:   $QT_VERSION"
echo "Arch: $ARCH"

# ─── 1. Homebrew dependencies ───────────────────────────────────────────────
echo "[1/4] Installing Homebrew dependencies..."
brew install googletest openssl cmake ninja doxygen --quiet
echo "Homebrew packages ready."

# ─── 2. Install Qt via aqt ──────────────────────────────────────────────────
echo "[2/4] Checking Qt $QT_VERSION..."

QT_BASE="$HOME/Qt"
QT_TARGET="mac_$([ "$ARCH" = "arm64" ] && echo clang_64 || echo clang_64)"
QT_DIR="$QT_BASE/$QT_VERSION/macos"

if [ ! -d "$QT_DIR" ]; then
    pip3 install aqtinstall --quiet
    python3 -m aqt install-qt mac desktop "$QT_VERSION" clang_64 -O "$QT_BASE"
fi

export Qt6_DIR="$QT_DIR"
export PATH="$QT_DIR/bin:$PATH"
echo "Qt dir: $QT_DIR"

# ─── 3. Configure ───────────────────────────────────────────────────────────
echo "[3/4] Configuring..."

CMAKE_ARCH_FLAG="-DCMAKE_OSX_ARCHITECTURES=$ARCH"
if [ "$ARCH" = "arm64" ]; then
    DEPLOYMENT_TARGET="14"
else
    DEPLOYMENT_TARGET="12"
fi

cmake -Bbuild \
      -DCMAKE_BUILD_TYPE=Release \
      -DSKIP_BUILD_TESTS=ON \
      "$CMAKE_ARCH_FLAG" \
      "-DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET" \
      "-DPACKAGE_VERSION_LABEL=$VERSION"

# ─── 4. Build & Package ─────────────────────────────────────────────────────
echo "[4/4] Building and packaging..."
cmake --build build --config Release -j"$(sysctl -n hw.logicalcpu)"

# Package with retry (codesign can be flaky)
for i in 1 2 3 4 5; do
    cmake --build build --config Release --target package && break
    echo "Package attempt $i failed, retrying..."
    sleep 2
done

echo ""
echo "Packages:"
ls -lh build/deskflow* 2>/dev/null || echo "No packages found"
echo ""
echo "To install: open the .dmg file from the build/ directory"
