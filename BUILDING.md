# Building Deskflow (with Internet Relay)

This fork adds internet relay and bidirectional peer control. You can get pre-built packages three ways:

1. **[GitHub Actions](#github-actions-recommended)** — automated, zero setup, builds all platforms
2. **[Docker build](#docker-build)** — Linux packages on any OS with Docker
3. **[Local build](#local-build)** — build natively on your machine

---

## GitHub Actions (recommended)

The easiest way to get all packages (Windows EXE, Linux DEB/RPM/Flatpak, macOS DMG) is to use the included GitHub Actions workflow.

### Step 1 — Push to GitHub

```bash
# Create a new GitHub repo (via github.com or gh CLI), then:
git remote set-url origin https://github.com/YOUR_USERNAME/deskflow.git
# OR add a new remote:
git remote add myfork https://github.com/YOUR_USERNAME/deskflow.git

git push myfork master
```

### Step 2 — Enable Actions

Go to your repository → **Actions** → click **"I understand my workflows, go ahead and enable them"**.

### Step 3 — Trigger a build

**Option A — Continuous build (auto on every push):**
Every push to `master` automatically creates/updates a pre-release named `continuous` with all packages.

**Option B — Tagged release:**
```bash
git tag v1.0.0
git push myfork v1.0.0
```
This creates a proper GitHub Release named `v1.0.0`.

**Option C — Manual trigger:**
Actions → **Build & Release** → **Run workflow** → enter an optional version label → **Run workflow**.

### Step 4 — Download packages

- Go to **Releases** (sidebar) to download the published packages.
- Or go to **Actions → Build & Release → latest run → Artifacts** to get individual packages before the release job completes.

### What gets built

| Package | Platform |
|---------|---------|
| `deskflow-*.exe` / `deskflow-*.msi` | Windows x64 |
| `deskflow-*.exe` / `deskflow-*.msi` | Windows ARM64 |
| `deskflow-*.deb` | Debian stable x86_64 / ARM64 |
| `deskflow-*.deb` | Ubuntu 24.04 x86_64 / ARM64 |
| `deskflow-*.rpm` | Fedora 43 x86_64 / ARM64 |
| `deskflow-*.rpm` | openSUSE Tumbleweed x86_64 |
| `deskflow-*.flatpak` | Linux (any distro) x86_64 / aarch64 |
| `deskflow-*.dmg` | macOS ARM64 (Apple Silicon) |
| `deskflow-*.dmg` | macOS x86_64 (Intel) |

---

## Docker Build

Build Linux packages on any OS that has Docker (including Windows with Docker Desktop).

```bash
# Build Debian .deb
./tools/build-docker.sh debian

# Build Ubuntu .deb
./tools/build-docker.sh ubuntu

# Build Fedora .rpm
./tools/build-docker.sh fedora

# Build openSUSE .rpm
./tools/build-docker.sh opensuse
```

Packages appear in `dist/`.

On **Windows** you need Docker Desktop running (WSL2 backend). Run the script from Git Bash or WSL.

---

## Local Build

### Windows (EXE + MSI)

**Prerequisites** (the script installs these automatically):
- Windows 10/11 x64 or ARM64
- [PowerShell 5.1+](https://docs.microsoft.com/en-us/powershell/)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) (or Build Tools for Visual Studio 2022) with "Desktop development with C++" workload
- Internet connection (for downloading Qt, vcpkg packages)

```powershell
# Run from the deskflow directory in an elevated PowerShell:
.\tools\build-windows.ps1

# Build for ARM64:
.\tools\build-windows.ps1 -Arch arm64

# Skip tool installation if already done:
.\tools\build-windows.ps1 -SkipToolInstall
```

Output: `build\deskflow-*.msi` (installer) and `build\deskflow-*.7z` (portable archive)

**Manual steps** (if you prefer to control the install):
```powershell
# 1. Install CMake and Ninja
scoop install cmake ninja

# 2. Install Qt 6 (via aqt)
pip install aqtinstall
python -m aqt install-qt windows desktop 6.10.2 win64_msvc2022_64 -O C:\Qt

# 3. Install WiX 4
dotnet tool install --global wix --version 4.0.5

# 4. Install vcpkg dependencies
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install openssl:x64-windows-release gtest:x64-windows-release

# 5. Configure & build
cmake -Bbuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKIP_BUILD_TESTS=ON `
      -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake `
      -DVCPKG_TARGET_TRIPLET=x64-windows-release
cmake --build build --target package -j8
```

---

### Linux (DEB / RPM)

```bash
chmod +x tools/build-linux.sh
./tools/build-linux.sh
```

The script auto-detects your distro (Debian/Ubuntu/Fedora/openSUSE/Arch) and installs the right packages.

**Output:**
- `build/deskflow-*.deb` — Debian/Ubuntu
- `build/deskflow-*.rpm` — Fedora/openSUSE

**Manual steps:**
```bash
# Debian/Ubuntu
sudo apt-get install cmake build-essential ninja-build \
  libx11-dev libxtst-dev libssl-dev libglib2.0-dev libxkbfile-dev \
  qt6-base-dev qt6-tools-dev libgtk-3-dev libgtest-dev \
  libei-dev libportal-dev help2man

cmake -Bbuild -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSKIP_BUILD_TESTS=ON -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --target package -j$(nproc)
sudo dpkg -i build/deskflow-*.deb
```

---

### macOS (DMG)

```bash
chmod +x tools/build-macos.sh
./tools/build-macos.sh
```

Requires Xcode Command Line Tools (`xcode-select --install`) and Homebrew.

---

## Installing

| Platform | Command |
|---------|---------|
| Windows | Run the `.msi` installer or extract the `.7z` archive |
| Debian/Ubuntu | `sudo dpkg -i deskflow-*.deb` |
| Fedora | `sudo rpm -ivh deskflow-*.rpm` |
| openSUSE | `sudo zypper install deskflow-*.rpm` |
| Flatpak | `flatpak install deskflow-*.flatpak` |
| macOS | Open the `.dmg` and drag to Applications |

---

## Relay Server

The relay server (`relay/server.js`) is a plain Node.js file — no build needed:

```bash
# Deploy on any Linux VPS / cloud instance
node --version  # needs Node.js ≥ 16

cd relay/
node server.js  # starts on port 24801

# OR with Docker:
docker compose up -d
```

See [relay/README.md](relay/README.md) for full deployment and configuration instructions.
