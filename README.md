# RG353V raylib demo

Demo app for:

- macOS local testing
- Anbernic RG353V (dArkOS / PortMaster flow)

The project vendors `raylib/` and uses SDL backend for RG353V builds.
`raylib` is tracked as a git submodule (pinned to a specific commit).

## Prerequisites (macOS)

```bash
brew install cmake zig rsync
```

## Build configs

### 1) `macos-debug`

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/rg353v-demo
```

### 2) `macos-release`

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/rg353v-demo
```

### 3) `rg353v-debug`

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Binary:

```bash
./build/rg353v-debug/rg353v-demo
```

### 4) `rg353v-final`

```bash
cmake --preset rg353v-final --fresh
cmake --build --preset rg353v-final
```

Binary:

```bash
./build/rg353v-final/rg353v-demo
```

## One-time RG353V sysroot setup

Pull sysroot from device:

```bash
./scripts/sync-rg353v-sysroot.sh <device-ip> [ssh-user]
```

Example:

```bash
./scripts/sync-rg353v-sysroot.sh 192.168.1.42 ark
```

Set this in `~/.zshrc`:

```bash
export RG353V_SYSROOT="/Users/ip/Projects/core_undo_redo/sysroot/rg353v"
```

Reload shell:

```bash
source ~/.zshrc
```

## Deploy to device

```bash
scp ./build/rg353v-final/rg353v-demo ark@<device-ip>:/roms2/ports/demo/
```

Example launcher (`/roms2/ports/demo.sh`):

```bash
#!/bin/bash
cd /roms2/ports/demo || exit 1
exec ./rg353v-demo
```

App exit combo inside demo: `START + SELECT`.
