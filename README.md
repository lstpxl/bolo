# BOLT (RG353V)

Demo app for:

- macOS local testing
- Anbernic RG353V (dArkOS / PortMaster flow)

The project vendors `third_party/raylib` and uses SDL backend for RG353V builds.
`raylib`, `raygui`, and `spdlog` are tracked as git submodules (pinned commits).

## Prerequisites (macOS)

```bash
brew install cmake zig rsync clang-format
```

## Build configs

Note: when CMake exports compile commands, the project root `compile_commands.json` is auto-synced to the active build directory file for IDE/clangd diagnostics.

### 1) `macos-debug`

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bolt
```

### 2) `macos-release`

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/bolt
```

### 3) `rg353v-debug`

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Binary:

```bash
./build/rg353v-debug/bolt
```

### 4) `rg353v-release`

```bash
cmake --preset rg353v-release --fresh
cmake --build --preset rg353v-release
```

Binary:

```bash
./build/rg353v-release/bolt
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
export RG353V_SYSROOT="/Users/ip/Projects/bolt/sysroot"
```

Reload shell:

```bash
source ~/.zshrc
```

## Deploy to device

```bash
bash scripts/deploy-rg353v.sh release <device-ip>
```

Or with npm scripts (VS Code friendly):

```bash
# one-time project setup:
cp .env.example .env
# then edit .env with your device IP/user/path

# build + deploy release:
npm run build-and-deploy:rg353v:release
```

Example launcher (`/roms2/ports/bolt.sh`):

scp -r /Users/ip/Projects/bolt/assets/bolt-image.png ark@192.168.100.86:/roms2/ports/images

```bash
#!/bin/bash
cd /roms2/ports/bolt || exit 1
exec ./bolt
```

App exit combo: `START + SELECT`.

## Logging

The app writes logs next to the executable:

- `bolt.log` – debug, info, warnings (resource loading, etc.)
- `profile.log` – profiling telemetry (`[PROFILE]`, `[ENEMY_*]`, etc.)

## Compare handheld profiling logs

Profile output goes to `profile.log` in the app directory. For handheld comparison, copy `profile.log` from the device after a session, then run:

```bash
python3 scripts/compare-handheld-profiles.py baseline-profile.log candidate-profile.log
```

Convention:

- first arg = baseline (usually old wiring)
- second arg = candidate (usually new wiring)

## Links

[Fire Bullets](https://bdragon1727.itch.io/fire-pixel-bullet-16x16)

[game-mechanics](https://monkeyslunch.com/resources/big-list-of-game-mechanics/)
[game-mechanics](https://www.squidi.net/three/index.php)

[PixelLab - AI Generator for Pixel Art Game Assets](https://www.pixellab.ai)
