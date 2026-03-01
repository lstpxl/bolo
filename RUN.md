# Run

Run commands from project root:

```bash
cd /Users/ip/Projects/bolt
```

## macOS Debug

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bolt
```

## macOS Release

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/bolt
```

## RG353V Debug (handheld)

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Binary:

```bash
./build/rg353v-debug/bolt
```

## RG353V Release (handheld)

```bash
cmake --preset rg353v-release --fresh
cmake --build --preset rg353v-release
```

Binary:

```bash
./build/rg353v-release/bolt
```

Deploy release build to device:

```bash
# direct deploy script:
bash scripts/deploy-rg353v.sh release <device-ip>

# or npm script workflow:
cp .env.example .env
# edit .env once with RG353V_DEVICE_IP
npm run build-and-deploy:rg353v:release
```

## If configure fails due to missing tools, install prerequisites

```bash
brew install cmake zig rsync
```
