# BOLO (RG353V)

Demo app for:

- macOS local testing
- Anbernic RG353V (dArkOS / PortMaster flow)

The project vendors `raylib/` and uses SDL backend for RG353V builds.
`raylib` and `raygui` are tracked as git submodules (pinned commits).

## Prerequisites (macOS)

```bash
brew install cmake zig rsync
```

## Build configs

### 1) `macos-debug`

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bolo
```

### 2) `macos-release`

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/bolo
```

### 3) `rg353v-debug`

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Binary:

```bash
./build/rg353v-debug/bolo
```

### 4) `rg353v-final`

```bash
cmake --preset rg353v-final --fresh
cmake --build --preset rg353v-final
```

Binary:

```bash
./build/rg353v-final/bolo
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

`rg353v-*` presets default `RG353V_SYSROOT` to `${sourceDir}/sysroot`.
If you run manual CMake commands outside presets, set:

```bash
export RG353V_SYSROOT="/Users/ip/Projects/bolo/sysroot"
```

Reload shell:

```bash
source ~/.zshrc
```

## Deploy to device

```bash
bash scripts/deploy-rg353v.sh final <device-ip>
```

Or with npm scripts (VS Code friendly):

```bash
# one-time project setup:
cp .env.example .env
# then edit .env with your device IP/user/path

# build + deploy final:
npm run ship:rg353v:final
```

Example launcher (`/roms2/ports/bolo.sh`):

```bash
#!/bin/bash
cd /roms2/ports/bolo || exit 1
exec ./bolo
```

App exit combo: `START + SELECT`.
