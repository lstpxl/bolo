# Run

Run commands from project root:

```bash
cd /Users/ip/Projects/bolo
```

## macOS Debug

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/rg353v-demo
```

## macOS Release

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/rg353v-demo
```

## RG353V Debug (handheld)

```bash
cmake --preset rg353v-debug --fresh
cmake --build --preset rg353v-debug
```

Binary:

```bash
./build/rg353v-debug/rg353v-demo
```

## RG353V Final (handheld)

```bash
cmake --preset rg353v-final --fresh
cmake --build --preset rg353v-final
```

Binary:

```bash
./build/rg353v-final/rg353v-demo
```

Deploy final build to device:

```bash
scp ./build/rg353v-final/rg353v-demo ark@<device-ip>:/roms2/ports/demo/
```

## If configure fails due to missing tools, install prerequisites

```bash
brew install cmake zig rsync
```
