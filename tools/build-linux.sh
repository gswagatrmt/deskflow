#!/usr/bin/env bash
# Build deskflow .deb or .rpm package on Linux.
# Auto-detects the distro and installs the right dependencies.
#
# Usage:
#   chmod +x scripts/build-linux.sh
#   ./scripts/build-linux.sh
#
# Output: build/deskflow*.deb  (Debian/Ubuntu)
#         build/deskflow*.rpm  (Fedora/openSUSE)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "=== Deskflow Linux Build Script ==="

# ─── Detect distro ──────────────────────────────────────────────────────────
if [ -f /etc/os-release ]; then
    . /etc/os-release
    LIKE="${ID_LIKE:-$ID}"
else
    LIKE="unknown"
fi

echo "Detected distro: $ID ($LIKE)"

# ─── 1. Install dependencies ────────────────────────────────────────────────
echo "[1/4] Installing build dependencies..."

if echo "$LIKE" | grep -q "debian\|ubuntu"; then
    sudo apt-get update -qq
    sudo apt-get install -y \
        cmake build-essential ninja-build \
        xorg-dev libx11-dev libxtst-dev libssl-dev \
        libglib2.0-dev libxkbfile-dev \
        qt6-base-dev qt6-tools-dev \
        libgtk-3-dev libgtest-dev libgmock-dev \
        libei-dev libportal-dev help2man doxygen

elif echo "$LIKE" | grep -q "fedora\|rhel\|centos"; then
    sudo dnf install -y \
        cmake make ninja-build gcc-c++ rpm-build openssl-devel \
        glib2-devel libXtst-devel libxkbfile-devel \
        qt6-qtbase-devel qt6-qttools-devel \
        gtk3-devel gtest-devel gmock-devel \
        libei-devel libportal-devel help2man doxygen

elif echo "$LIKE" | grep -q "suse\|opensuse"; then
    sudo zypper refresh
    sudo zypper install -y --force-resolution \
        cmake make ninja gcc-c++ rpm-build libopenssl-devel \
        glib2-devel libXtst-devel libxkbfile-devel \
        qt6-base-devel qt6-tools-devel qt6-linguist-devel \
        gtk3-devel googletest-devel googlemock-devel \
        libei-devel libportal-devel help2man doxygen

elif echo "$LIKE" | grep -q "arch"; then
    sudo pacman -Syu --noconfirm \
        base-devel cmake ninja \
        gcc openssl glib2 libxtst libxkbfile \
        gtest libei libportal \
        qt6-base qt6-tools qt6-svg qt6-translations qt6-declarative \
        gtk3 help2man doxygen

else
    echo "Unsupported distro: $ID. Please install dependencies manually."
    echo "Required: cmake ninja Qt6 OpenSSL libXtst libxkbfile gtk3"
    exit 1
fi

echo "Dependencies installed."

# ─── 2. Configure ───────────────────────────────────────────────────────────
echo "[2/4] Configuring..."

VERSION="${1:-1.0.0-relay}"
cmake -Bbuild \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DSKIP_BUILD_TESTS=ON \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DPACKAGE_VERSION_LABEL="$VERSION"

# ─── 3. Build & Package ─────────────────────────────────────────────────────
echo "[3/4] Building and packaging..."
cmake --build build --config Release --target package -j"$(nproc)"

# ─── 4. Show output ─────────────────────────────────────────────────────────
echo "[4/4] Packages:"
ls -lh build/deskflow* 2>/dev/null || echo "No packages found in build/"
echo ""
echo "Done! Install with:"
if echo "$LIKE" | grep -q "debian\|ubuntu"; then
    echo "  sudo dpkg -i build/deskflow*.deb"
    echo "  sudo apt-get install -f   # fix any dependency issues"
elif echo "$LIKE" | grep -q "fedora\|rhel\|centos\|suse\|opensuse"; then
    echo "  sudo rpm -ivh build/deskflow*.rpm"
fi
