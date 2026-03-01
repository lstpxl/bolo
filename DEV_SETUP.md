# Development Setup (macOS)

## Submodule cloning

```bash
git clone --recurse-submodules <repo-url>
# or, after clone:
git submodule sync --recursive
git submodule update --init --recursive
```

This guide reproduces the same development environment on another Mac.

## 1) Prerequisites

Install Xcode Command Line Tools:

```bash
xcode-select --install
```

Install Homebrew (if missing):  
`https://brew.sh`

Install required tools:

```bash
brew install cmake zig rsync
```

Verify:

```bash
cmake --version
zig version
rsync --version
```

## 2) Clone project

```bash
git clone --recurse-submodules <your-repo-url> bolt
cd bolt
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## 3) Build and run on macOS

Debug:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bolt
```

Release:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/bolt
```

## 4) RG353V cross-compile setup (one time)

Pull sysroot from device:

```bash
./scripts/sync-rg353v-sysroot.sh <device-ip> [ssh-user]
```

Example:

```bash
./scripts/sync-rg353v-sysroot.sh 192.168.1.42 ark
```

`rg353v-*` presets default `RG353V_SYSROOT` to `${sourceDir}/sysroot`.
If you run manual CMake commands outside presets, set:

```bash
export RG353V_SYSROOT="/absolute/path/to/bolt/sysroot"
```

Reload shell:

```bash
source ~/.zshrc
```

## 5) Build RG353V binaries

Debug:

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Release:

```bash
cmake --preset rg353v-release --fresh
cmake --build --preset rg353v-release
```

Output:

```bash
./build/rg353v-release/bolt
```

## 6) Deploy to RG353V

```bash
scp ./build/rg353v-release/bolt ark@<device-ip>:/roms2/ports/bolt/
```

Suggested launcher `/roms2/ports/bolt.sh`:

```bash
#!/bin/bash
cd /roms2/ports/bolt || exit 1
exec ./bolt
```
