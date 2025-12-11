# Building minirend with WSL (Windows Subsystem for Linux)

The cosmo-sokol shims require Linux system headers (`/usr/include/X11`, `/usr/include/GL`) to compile the Linux/OpenGL backend. On Windows, use WSL for local cross-platform builds.

## Quick Start

If you don't have WSL set up, run this command in **PowerShell as Administrator**:

```powershell
wsl --install -d Ubuntu
```

This will install WSL2 and Ubuntu. **Restart your computer when prompted.**

After restart, Ubuntu will launch automatically to complete setup. Create a username and password when prompted.

## Prerequisites (if WSL is already installed)

### 1. Check WSL status

```powershell
wsl --list --verbose
```

### 2. If no distributions are installed

```powershell
wsl --install -d Ubuntu
```

### 3. Set up the build environment in WSL

Open WSL (Ubuntu) and install required packages:

```bash
# Update package lists
sudo apt update

# Install build essentials
sudo apt install -y build-essential git python3 zip unzip

# Install X11 and OpenGL development headers (required for sokol Linux shims)
sudo apt install -y libx11-dev libxcursor-dev libxrandr-dev libxi-dev libgl-dev

# Optional: Install parallel for faster builds
sudo apt install -y parallel
```

## Building

### Option A: Build from WSL directly

1. Navigate to your project (Windows drives are mounted under `/mnt/`):

```bash
cd /mnt/c/dev/minirend
```

2. Run the build:

```bash
./build_scripts/build
```

### Option B: Use the WSL build script from PowerShell

From PowerShell in your project directory:

```powershell
wsl bash -c "cd /mnt/c/dev/minirend && ./build_scripts/build"
```

### Option C: Use the Configurator with WSL

The configurator can be configured to use WSL for builds. Update `build_scripts/config_server.js` to use WSL:

```javascript
// In findBash(), add WSL option:
if (process.platform === 'win32') {
  return 'wsl';  // Use WSL's bash
}
```

## Setting up the Linux shim symlinks

The cosmo-sokol approach uses symlinks to system headers. In WSL, create them:

```bash
cd /mnt/c/dev/minirend/src/platform/shims

# Create symlinks to system headers
ln -sf /usr/include/X11 X11
ln -sf /usr/include/GL GL
ln -sf /usr/include/KHR KHR
```

Note: These symlinks will only work when building from WSL.

## Troubleshooting

### "Permission denied" errors

If you get permission errors, ensure the scripts are executable:

```bash
chmod +x build_scripts/build
chmod +x build_scripts/bootstrap_deps
```

### "cosmocc not found"

The bootstrap script will download cosmocc automatically. If it fails, download manually:

```bash
cd /mnt/c/dev/minirend/third_party
mkdir -p cosmocc
cd cosmocc
wget https://github.com/jart/cosmopolitan/releases/latest/download/cosmocc-4.0.2.zip
unzip cosmocc-*.zip
```

### X11/GL headers not found

Make sure you installed the development packages:

```bash
sudo apt install -y libx11-dev libxcursor-dev libxrandr-dev libxi-dev libgl-dev
```

## Quick Start Script

Save this as `build-wsl.sh` in your project root:

```bash
#!/bin/bash
set -e

echo "==> Building minirend in WSL"

# Ensure we're in the project directory
cd "$(dirname "$0")"

# Check for required packages
if ! dpkg -s libx11-dev >/dev/null 2>&1; then
    echo "==> Installing required packages..."
    sudo apt update
    sudo apt install -y build-essential libx11-dev libxcursor-dev libxrandr-dev libxi-dev libgl-dev
fi

# Create symlinks if needed
if [ ! -L src/platform/shims/X11 ]; then
    echo "==> Creating header symlinks..."
    cd src/platform/shims
    ln -sf /usr/include/X11 X11
    ln -sf /usr/include/GL GL  
    ln -sf /usr/include/KHR KHR 2>/dev/null || true
    cd ../../..
fi

# Run the build
./build_scripts/build

echo "==> Build complete!"
```

Then run from PowerShell:

```powershell
wsl bash ./build-wsl.sh
```

