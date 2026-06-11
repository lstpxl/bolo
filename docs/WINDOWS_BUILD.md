# Windows Build Guide

This guide is for finishing the Windows desktop build on a Windows machine. CMake presets, npm scripts, and VS Code tasks are already prepared in the repo; your job is to install tooling, build, run, and verify.

Windows builds use the same desktop path as macOS: raylib’s default GLFW backend with `TARGET_RG353V=OFF`. No SDL sysroot or cross-compilation is involved.

## What is already prepared (Mac side)

| Item | Location |
|------|----------|
| CMake presets `windows-debug`, `windows-release` | `CMakePresets.json` |
| MSVC warning flags, Windows `compile_commands.json` copy | `CMakeLists.txt` |
| npm build/run scripts | `package.json` |
| VS Code tasks | `.vscode/tasks.json` |

## What you do on Windows

| Step | Action |
|------|--------|
| 1 | Install prerequisites (below) |
| 2 | Clone repo and init submodules |
| 3 | Configure and build with CMake presets |
| 4 | Run `bolt.exe` and complete the verification checklist |
| 5 | Fix any MSVC-only compile issues and open a PR if needed |

---

## Prerequisites

Install these before building:

### Required

1. **Git** — [https://git-scm.com/download/win](https://git-scm.com/download/win)

2. **CMake ≥ 3.25**

   ```powershell
   winget install Kitware.CMake
   ```

   Or download from [https://cmake.org/download/](https://cmake.org/download/). Ensure `cmake` is on your `PATH`.

3. **Visual Studio 2022 Build Tools** with the **Desktop development with C++** workload

   - Download: [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)
   - In the installer, select **Desktop development with C++**
   - This provides MSVC v143 and the Windows SDK

4. **Ninja** — usually bundled with Visual Studio. If `ninja` is not found:

   ```powershell
   winget install Ninja-build.Ninja
   ```

### Optional

- **Node.js** — only if you want `npm run build:windows` and related scripts
- **VS Code** + extensions:
  - CMake Tools (`ms-vscode.cmake-tools`)
  - C/C++ (`ms-vscode.cpptools`)

---

## One-time repo setup

```powershell
git clone <repo-url> bolt
cd bolt
git submodule update --init --recursive
```

Submodules (`raylib`, `raygui`, `spdlog`) are required. A fresh clone without submodules will fail at configure time.

---

## Build and run

Open **x64 Native Tools Command Prompt for VS 2022** or a PowerShell session where `cl`, `cmake`, and `ninja` are available. From the repo root:

### Debug (recommended for first build)

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\windows-debug\bolt.exe
```

### Release

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
.\build\windows-release\bolt.exe
```

### npm scripts (optional)

```powershell
npm run build:windows
npm run run:windows

# or build + run in one step:
npm run build-and-run:windows

# release:
npm run build:windows:release
```

---

## How resources are found

The game loads assets from the repo-root `resources/` folder (audio, textures, fonts). `ResourceLocator` searches relative paths from the executable directory and walks up parent directories.

With the default layout:

```text
bolt/
  resources/          ← assets live here
  build/
    windows-debug/
      bolt.exe        ← binary runs from here
```

`../../resources/` resolves correctly. **No post-build copy step is needed for local development.**

### Distributing a build

To ship the game outside the repo tree, copy these together:

```text
bolt.exe
resources/          ← entire folder
```

Logs (`bolt.log`, `profile.log`) are written next to `bolt.exe` at runtime.

---

## Verification checklist

Complete this after your first successful build:

- [ ] Game window opens at expected resolution
- [ ] Main menu renders (fonts and textures load from `resources/`)
- [ ] Audio plays (menu navigation click, gameplay sounds after starting)
- [ ] Keyboard input responds (arrow keys, Enter, Escape)
- [ ] Gamepad input responds if a controller is connected
- [ ] `bolt.log` appears in `build\windows-debug\` after running
- [ ] `profile.log` appears in `build\windows-debug\` after gameplay
- [ ] Release build (`windows-release`) configures, builds, and runs

If textures or audio are missing, check `bolt.log` for `failed to load` warnings before filing an issue.

---

## Troubleshooting

| Symptom | Likely fix |
|---------|------------|
| `windows-debug` preset not listed | Presets are host-gated to Windows. Run CMake on a Windows machine, not WSL unless you add separate WSL presets. |
| `ninja` not found | Install Ninja (`winget install Ninja-build.Ninja`) or use **x64 Native Tools Command Prompt for VS 2022**. |
| MSVC / compiler not detected | Re-run VS Build Tools installer; ensure **Desktop development with C++** is checked. Open a VS developer shell. |
| `cmake` not found | Re-open terminal after install; confirm `cmake --version` ≥ 3.25. |
| Missing textures or audio | Confirm `resources/` exists at repo root. Run `bolt.exe` from the default build output path. |
| Submodule / raylib errors | Run `git submodule update --init --recursive` again. |
| Build succeeds but window is blank | Check `bolt.log` for resource load failures. |

---

## First-build fixes (MSVC)

The Mac maintainer cannot compile-test MSVC locally. Your first Windows build may surface compiler errors or warnings that only appear on MSVC.

If the build fails:

1. Capture the **full** configure and build log.
2. Try these common patterns:

   | Issue | Typical fix |
   |-------|-------------|
   | `min`/`max` macro conflicts | Rare with raylib; if seen, ensure `NOMINMAX` is defined before Windows headers |
   | POSIX types (`ssize_t`, `unistd.h`) | Not expected in this codebase; report if found |
   | `/W4` warnings treated as errors | Fix the warning in source, or temporarily lower to `/W3` in `CMakeLists.txt` while investigating |

3. Open a PR with any MSVC-specific source or CMake fixes you discover.

---

## VS Code on Windows

The committed `.vscode/settings.json` defaults to macOS presets. Override locally (User or Workspace settings, not committed):

```json
{
  "cmake.defaultConfigurePreset": "windows-debug",
  "cmake.defaultBuildPreset": "windows-debug",
  "C_Cpp.default.intelliSenseMode": "windows-msvc-x64",
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build/windows-debug"
  ]
}
```

Available tasks (Terminal → Run Task):

- **BOLT: Build Windows Debug**
- **BOLT: Build Windows Release**
- **BOLT: Run Windows**
- **BOLT: Build and Run Windows**

---

## Platform notes

| Topic | Windows behavior |
|-------|------------------|
| Presentation scale | `1×` (macOS uses `2×` for Retina). Intentional. |
| Debug maze click | macOS debug builds only. Not required for Windows. |
| Memory profiling | `Profiling.cpp` allocation tracking returns `0` on Windows (no `malloc_size`). Safe to ignore. |
| Exit combo | `START + SELECT` on gamepad (same as RG353V). |

---

## Out of scope (for now)

- **MinGW** — not configured; MSVC + Ninja is the supported toolchain. MinGW presets can be added later if needed.
- **Cross-compile from macOS** — not set up. Build natively on Windows.
- **CI** — no GitHub Actions Windows job yet; optional follow-up.

---

## Quick reference

```powershell
# First-time setup
git submodule update --init --recursive

# Debug build + run
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\windows-debug\bolt.exe

# Release build + run
cmake --preset windows-release
cmake --build --preset windows-release
.\build\windows-release\bolt.exe
```
