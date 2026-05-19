#!/usr/bin/env bash
# Build Linux packages (deb/rpm) using Docker.
# Works on Windows (Docker Desktop), macOS, and Linux.
#
# Usage:
#   ./scripts/build-docker.sh              # builds Debian .deb
#   ./scripts/build-docker.sh fedora       # builds Fedora .rpm
#   ./scripts/build-docker.sh ubuntu       # builds Ubuntu .deb
#   ./scripts/build-docker.sh opensuse     # builds openSUSE .rpm

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-debian}"
VERSION="${2:-1.0.0-relay}"
OUT="$ROOT/dist"

mkdir -p "$OUT"

echo "=== Docker Linux Build ($TARGET) ==="

case "$TARGET" in
  debian)
    IMAGE="debian:stable-slim"
    PKG_CMD="apt-get update -qq && apt-get install -y cmake build-essential ninja-build xorg-dev libx11-dev libxtst-dev libssl-dev libglib2.0-dev libxkbfile-dev qt6-base-dev qt6-tools-dev libgtk-3-dev libgtest-dev libgmock-dev libei-dev libportal-dev help2man doxygen"
    ;;
  ubuntu)
    IMAGE="ubuntu:24.04"
    PKG_CMD="apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y cmake build-essential ninja-build xorg-dev libx11-dev libxtst-dev libssl-dev libglib2.0-dev libxkbfile-dev qt6-base-dev qt6-tools-dev libgtk-3-dev libgtest-dev libgmock-dev libei-dev libportal-dev help2man doxygen"
    ;;
  fedora)
    IMAGE="fedora:43"
    PKG_CMD="dnf install -y cmake make ninja-build gcc-c++ rpm-build openssl-devel glib2-devel libXtst-devel libxkbfile-devel qt6-qtbase-devel qt6-qttools-devel gtk3-devel gtest-devel gmock-devel libei-devel libportal-devel help2man doxygen"
    ;;
  opensuse)
    IMAGE="opensuse/tumbleweed:latest"
    PKG_CMD="zypper refresh && zypper install -y --force-resolution cmake make ninja gcc-c++ rpm-build libopenssl-devel glib2-devel libXtst-devel libxkbfile-devel qt6-base-devel qt6-tools-devel qt6-linguist-devel gtk3-devel googletest-devel googlemock-devel libei-devel libportal-devel help2man doxygen"
    ;;
  *)
    echo "Unknown target: $TARGET. Use: debian, ubuntu, fedora, opensuse"
    exit 1
    ;;
esac

BUILD_SCRIPT=$(cat <<SCRIPT
set -ex
$PKG_CMD
cd /src
cmake -Bbuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKIP_BUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=/usr -DPACKAGE_VERSION_LABEL="$VERSION"
cmake --build build --config Release --target package -j\$(nproc)
cp build/deskflow* /out/ 2>/dev/null || true
SCRIPT
)

docker run --rm \
    -v "$ROOT:/src:ro" \
    -v "$OUT:/out" \
    "$IMAGE" \
    bash -c "$BUILD_SCRIPT"

echo ""
echo "=== Packages in $OUT/ ==="
ls -lh "$OUT"/deskflow* 2>/dev/null || echo "No packages found"
