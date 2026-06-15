# Windows Build Guide

This guide is for finishing the Windows desktop build on a Windows machine. CMake presets, npm scripts, and VS Code tasks are already prepared in the repo; your job is to install tooling, build, run, and verify.

Windows builds use the same desktop path as macOS: raylib’s default GLFW backend with `TARGET_RG353V=OFF`. No SDL sysroot or cross-compilation is involved.

## What is already prepared (Mac side)

| Item | Location |
|------|----------|
| CMake presets `windows-debug`, `windows-release` | `CMakePresets.json` |
| MSVC warning flags, Windows `compile_commands.json` copy, GUI subsystem, static CRT (`/MT`) | `CMakeLists.txt` |
| npm build/run scripts | `package.json` |
| VS Code tasks | `.vscode/tasks.json` |

## What you do on Windows

| Step | Action |
|------|----------|
| 1 | Install prerequisites (below) |
| 2 | Clone repo and init submodules |
| 3 | Configure and build with CMake presets (**x64** developer shell) |
| 4 | Run `bolt.exe` and complete the verification checklist |
| 5 | For sharing: build **Release**, verify **x64**, ship with `resources/` |
| 6 | Fix any MSVC-only compile issues and open a PR if needed |

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
   - This provides MSVC and the Windows SDK

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

## Toolchain: always use x64

Windows presets use the **Ninja** generator. With Ninja + MSVC, **32-bit vs 64-bit is determined entirely by the Visual Studio developer shell** — not by CMake preset fields. Do **not** add `"architecture": { "value": "x64" }` to `CMakePresets.json` for Ninja; configure will fail with:

```text
Ninja does not support platform specification, but platform x64 was specified
```

### Open the correct shell

Use **Start → x64 Native Tools Command Prompt for VS 2022**.

Do **not** use:

- plain Command Prompt or PowerShell (no `cl`)
- **x86** Native Tools Command Prompt (produces a 32-bit exe)

Verify before configuring:

```cmd
where cl
```

The path must contain `Hostx64\x64\cl.exe`, not `Hostx86\x86`.

Configure output should also show:

```text
.../Hostx64/x64/cl.exe
```

### Verify the built executable

After linking, confirm the binary is 64-bit:

```cmd
dumpbin /headers build\windows-release\bolt.exe | findstr machine
```

You want:

```text
8664 machine (x64)
```

If you see `14C machine (x86)`, you built 32-bit by mistake. Delete the build directory, switch to the **x64** developer shell, and reconfigure from scratch:

```cmd
rmdir /s /q build\windows-release
cmake --preset windows-release
cmake --build --preset windows-release
```

(PowerShell equivalent: `Remove-Item -Recurse -Force build\windows-release`.)

---

## One-time repo setup

```cmd
git clone <repo-url> bolt
cd bolt
git submodule update --init --recursive
```

Submodules (`raylib`, `raygui`, `spdlog`) are required. A fresh clone without submodules will fail at configure time.

---

## Build and run

Open **x64 Native Tools Command Prompt for VS 2022**. From the repo root:

### Debug (recommended for first build)

Use debug builds **only on your dev machine**. Do not ship them (see [Distributing a build](#distributing-a-build)).

```cmd
cmake --preset windows-debug
cmake --build --preset windows-debug
build\windows-debug\bolt.exe
```

### Release

Use release for performance testing and for builds you share with others.

```cmd
cmake --preset windows-release
cmake --build --preset windows-release
build\windows-release\bolt.exe
```

### npm scripts (optional)

Requires Node.js. Still run from an **x64** developer shell so the underlying build is 64-bit.

```powershell
npm run build:windows
npm run run:windows

# or build + run in one step:
npm run build-and-run:windows

# release:
npm run build:windows:release
```

### Windows executable settings

Windows desktop builds (`TARGET_RG353V=OFF`) are configured in `CMakeLists.txt` as:

| Setting | Effect |
|---------|--------|
| `/SUBSYSTEM:WINDOWS` + `/ENTRY:mainCRTStartup` | **GUI-only** — one game window, no extra console |
| `/MT` (Release) / `/MTd` (Debug) | **Static MSVC runtime** — no `VCRUNTIME140.dll` / `MSVCP140.dll` dependency |

After changing these flags (or pulling an update that changes them), delete the build directory and reconfigure so cached `/MD` objects are not reused:

```cmd
rmdir /s /q build\windows-release
cmake --preset windows-release
cmake --build --preset windows-release
```

Optional: confirm the release binary does not depend on MSVC runtime DLLs:

```cmd
dumpbin /dependents build\windows-release\bolt.exe
```

You should **not** see `VCRUNTIME140.dll` or `MSVCP140.dll`. System DLLs such as `KERNEL32.dll` and `OPENGL32.dll` are expected.

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

---

## Distributing a build

### What to ship

Always distribute a **Release** build. Copy these together into one folder:

```text
your-folder/
  bolt.exe          ← from build\windows-release\bolt.exe (must be x64)
  resources/        ← entire folder from repo root (audio, fonts, textures, …)
```

Logs (`bolt.log`, `profile.log`) are created next to `bolt.exe` at runtime — do not include them in the package.

### What recipients need

Release builds use the **static** MSVC runtime (`/MT`). Recipients do **not** need to install the Visual C++ Redistributable for `VCRUNTIME140.dll` / `MSVCP140.dll`.

The game still uses normal Windows system libraries (OpenGL, kernel, etc.) and the **Universal C Runtime** (`ucrtbase.dll`), which is included in Windows 10 and later.

If someone runs an **older** build that was linked with `/MD` (dynamic runtime), they may need [VC++ Redistributable x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist). Rebuild from current `main` to pick up static `/MT`.

### Do not ship Debug builds

Debug binaries depend on **debug-only** runtime DLLs such as `ucrtbased.dll`, `VCRUNTIME140D.dll`, and `MSVCP140D.dll`. These exist only on machines with Visual Studio / Build Tools installed.

Symptoms on another PC:

- “Cannot continue because `ucrtbased.dll` was not found”
- Copying random DLLs next to `bolt.exe` → error `0xc000007b` (wrong architecture or incomplete runtime set)

**Never share `build\windows-debug\bolt.exe` outside your dev machine.**

### Test before sending

1. Confirm **`8664 machine (x64)`** with `dumpbin` (see [Toolchain: always use x64](#toolchain-always-use-x64)).
2. Copy **only** `bolt.exe` and `resources/` to a **new folder outside the repo** (e.g. `D:\bolo-ship\`).
3. Run `bolt.exe` from that folder — menu, fonts, and audio should work.
4. Optionally test on a PC **without** Visual Studio installed to confirm the package is self-contained.

### Diagnostic batch file for recipients

If the game fails on someone else’s machine, include a `run-bolt.bat` next to `bolt.exe`:

```bat
@echo off
cd /d "%~dp0"
echo Running from: %CD%
dir bolt.exe
dir resources
echo.
bolt.exe
echo Exit code: %ERRORLEVEL%
pause
```

Common exit codes:

| Exit code | Meaning |
|-----------|---------|
| `0` | Normal exit |
| `-1073741819` (`0xC0000005`) | Access violation — often wrong architecture (x86 build) or GPU/OpenGL crash during startup |
| `0xc000007b` | Invalid image format — usually 32/64-bit DLL mismatch |

An empty `bolt.log` with a crash exit code means the process died during very early startup (often `InitWindow` / OpenGL), before logging flushed.

---

## Verification checklist

Complete this after your first successful build:

- [ ] Configure log shows `Hostx64/x64/cl.exe`
- [ ] `dumpbin` reports `8664 machine (x64)` for release `bolt.exe`
- [ ] Game window opens at expected resolution
- [ ] Main menu renders (fonts and textures load from `resources/`)
- [ ] Audio plays (menu navigation click, gameplay sounds after starting)
- [ ] Keyboard input responds (arrow keys, Enter, Escape)
- [ ] Gamepad input responds if a controller is connected
- [ ] `bolt.log` appears in `build\windows-debug\` after running
- [ ] `profile.log` appears in `build\windows-debug\` after gameplay
- [ ] Release build (`windows-release`) configures, builds, and runs
- [ ] Only one window opens (title `bolt`) — no extra console window
- [ ] Release build runs from a standalone folder (`bolt.exe` + `resources/` only)
- [ ] `dumpbin /dependents` on release `bolt.exe` does not list `VCRUNTIME140.dll` or `MSVCP140.dll`

If textures or audio are missing, check `bolt.log` for `failed to load` warnings before filing an issue.

---

## Troubleshooting

| Symptom | Likely fix |
|---------|------------|
| `cl` / `cmake` / `ninja` not recognized | Open **x64 Native Tools Command Prompt for VS 2022**, not plain cmd/PowerShell. |
| `windows-debug` / `windows-release` preset not listed | Presets are host-gated to Windows. Run CMake on a Windows machine, not WSL. |
| Ninja + `platform x64 was specified` | Remove `"architecture"` from Windows presets (Ninja does not support it). Delete stale `build\windows-*` and reconfigure. |
| `compile_commands.json` COPY error on first configure | Fixed in `CMakeLists.txt` (deferred copy on Windows). Pull latest; delete `build\windows-debug` and reconfigure. |
| `dumpbin` shows `14C machine (x86)` | Wrong developer shell. Use **x64** Native Tools, delete build dir, rebuild. |
| `ucrtbased.dll` missing on another PC | You shipped a **Debug** build. Build and ship **Release** instead. |
| `0xc000007b` after copying MSVC DLLs by hand | Wrong DLL bitness or debug/runtime mix. Ship x64 **Release**; do not bundle random runtime DLLs. |
| Exit code `-1073741819` / empty `bolt.log` on another PC | Verify x64 release build; check Event Viewer → Application for faulting module (often GPU/OpenGL driver DLL). |
| `ninja` not found | Install Ninja (`winget install Ninja-build.Ninja`) or use **x64 Native Tools Command Prompt**. |
| MSVC / compiler not detected | Re-run VS Build Tools installer; ensure **Desktop development with C++** is checked. |
| `cmake` not found | Re-open terminal after install; confirm `cmake --version` ≥ 3.25. |
| Missing textures or audio | Confirm `resources/` exists next to `bolt.exe` (or repo layout for dev). Check `bolt.log`. |
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
   | `std::aligned_alloc` not found (`Profiling.cpp`) | Fixed: MSVC uses `_aligned_malloc` / `_aligned_free`. Pull latest. |
   | `min`/`max` macro conflicts | Rare with raylib; if seen, ensure `NOMINMAX` is defined before Windows headers |
   | POSIX types (`ssize_t`, `unistd.h`) | Not expected in this codebase; report if found |
   | `/W4` warnings treated as errors | Fix the warning in source, or temporarily lower to `/W3` in `CMakeLists.txt` while investigating |

3. Open a PR with any MSVC-specific source or CMake fixes you discover.

Build warnings in third-party headers (`raygui.h`) and some conversion warnings in game code are expected on first Windows builds and do not block linking.

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

Configure/build from an **x64** developer environment (CMake Tools kit or terminal).

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
| Runtime linking | Static `/MT` (Release) and `/MTd` (Debug) — no VC++ Redistributable required for MSVC runtime DLLs |
| Executable subsystem | GUI-only (`/SUBSYSTEM:WINDOWS`) — single game window, logs in `bolt.log` |

---

## Out of scope (for now)

- **MinGW** — not configured; MSVC + Ninja is the supported toolchain. MinGW presets can be added later if needed.
- **Cross-compile from macOS** — not set up. Build natively on Windows.
- **CI** — no GitHub Actions Windows job yet; optional follow-up.

---

## Quick reference

```cmd
REM First-time setup (x64 Native Tools Command Prompt)
git submodule update --init --recursive

REM Debug build + run (dev machine only)
cmake --preset windows-debug
cmake --build --preset windows-debug
build\windows-debug\bolt.exe

REM Release build + run (use this for sharing)
cmake --preset windows-release
cmake --build --preset windows-release
dumpbin /headers build\windows-release\bolt.exe | findstr machine
build\windows-release\bolt.exe

REM Ship: build\windows-release\bolt.exe + resources\
```
