# Development Setup (macOS)

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
git clone --recurse-submodules <your-repo-url> core_undo_redo
cd core_undo_redo
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
./build/macos-debug/rg353v-demo
```

Release:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/rg353v-demo
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

Set environment variable in `~/.zshrc`:

```bash
export RG353V_SYSROOT="/absolute/path/to/core_undo_redo/sysroot/rg353v"
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

Final:

```bash
cmake --preset rg353v-final --fresh
cmake --build --preset rg353v-final
```

Output:

```bash
./build/rg353v-final/rg353v-demo
```

## 6) Deploy to RG353V

```bash
scp ./build/rg353v-final/rg353v-demo ark@<device-ip>:/roms2/ports/demo/
```

Suggested launcher `/roms2/ports/demo.sh`:

```bash
#!/bin/bash
cd /roms2/ports/demo || exit 1
exec ./rg353v-demo
```
