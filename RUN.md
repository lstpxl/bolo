# Run

Run commands from project root:

```bash
cd /Users/ip/Projects/bolo
```

## macOS Debug

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bolo
```

## macOS Release

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/bolo
```

## RG353V Debug (handheld)

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Binary:

```bash
./build/rg353v-debug/bolo
```

## RG353V Final (handheld)

```bash
cmake --preset rg353v-final --fresh
cmake --build --preset rg353v-final
```

Binary:

```bash
./build/rg353v-final/bolo
```

Deploy final build to device:

```bash
# direct deploy script:
bash scripts/deploy-rg353v.sh final <device-ip>

# or npm script workflow:
cp .env.example .env
# edit .env once with RG353V_DEVICE_IP
npm run ship:rg353v:final
```

## If configure fails due to missing tools, install prerequisites

```bash
brew install cmake zig rsync
```
