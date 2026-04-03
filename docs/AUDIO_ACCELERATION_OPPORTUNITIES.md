# Audio acceleration opportunities

This document records profiling findings and concrete optimization options for the audio subsystem on handheld hardware (Anbernic RG353V, miniaudio/ALSA backend, raylib 5.6-dev).

**Current status (as of profile-53):**

- `frame.render` and `frame.fixed_step` are within budget.
- `frame.cpu_work` exceeds the 15 ms target in ~2–19% of 120-frame windows depending on combat intensity.
- The primary driver of those overruns is `music.update_stream` stalls (single uploads up to ~14 ms), compounded in peak windows by `system.ai_update`.
- PulseAudio runtime misconfiguration (`Failed to create secure directory (/run/user/1000//pulse)`) was confirmed as a contributing factor up to profile-50 and was resolved by correcting `XDG_RUNTIME_DIR` ownership on device.
- `kSampleBufferSamples` A/B tested across 1024 / 1536 / 2048 / 4096; **1536 is the current setting** as the best observed tradeoff (fewer spike events than 1024, lower per-upload synthesis cost than 2048/4096).

**Profiling infrastructure in place:**

- Per-upload `steady_clock` timing in `MenuMusicPlayer::Update` with immediate `[MUSIC_UPLOAD_SPIKE]` log to `bolt.log` for uploads > 5 ms.
- Per-instance rolling-window `[MUSIC_PLAYER_WINDOW]` log with `total / avg / max upload time` and `slow` count per 120-frame window.
- `[AUDIO_ROUTE_WINDOW]` per-window SFX event and sound-played counters with per-event-type breakdown.

---

## Remaining opportunities

### 1. Move `MenuMusicPlayer::Update` to a dedicated audio thread

**Problem:** `FillBuffer` (synthesis) + `UpdateAudioStream` (upload) both block the main thread. At 1536 samples / 16 kHz, each buffer covers ~96 ms of audio, so uploads are infrequent but can stall for 8–14 ms when the ALSA/PulseAudio stack is slow.

**Proposed model:**

```
Main thread                      Audio thread
────────────────                 ──────────────────────────────────
Write atomics:                   Loop every ~46 ms (half-buffer):
  enabled (bool)                   Read enabled / tense atomics
  tense (bool)                     if enabled:
                                     FillBuffer (synthesis)
                                     UpdateAudioStream (upload)
                                     IsAudioStreamProcessed check
```

**Thread-safety status (from raylib raudio.c source):** `UpdateAudioStream` and `IsAudioStreamProcessed` both acquire `AUDIO.System.lock` (a `ma_mutex`). They are **mutex-safe to call from any thread** in current raylib. The device callback holds the same lock during mixing, so contention can still occur, but it moves entirely out of `frame.cpu_work`.

**Signals from main thread (all trivially atomic or rarely-written):**
- `std::atomic<bool> enabled`
- `std::atomic<bool> tense`
- `std::atomic<bool> shutdown`

**What to avoid in audio thread:** Do not call `ma_device_start/stop/init/uninit` from the audio thread (miniaudio restriction; causes deadlock). `LoadAudioStream` / `UnloadAudioStream` must remain on the main thread during init/shutdown, with a handshake (e.g. `std::atomic<bool> streamReady`) before the audio thread starts consuming.

**Expected gain:** `gameplay.music` and `music.update_stream` effectively removed from `frame.cpu_work`; main-thread frame budget becomes `render + fixed_step + audio_route_sfx only`.

**Complexity:** Moderate. New `std::thread`, two atomics, careful init/shutdown ordering.

---

### 2. Move `AudioEventRouter::RouteStep` to the audio thread

**Problem:** `PlaySound` / `SetSoundVolume` (via `SoundPool::Play`) acquire `AUDIO.System.lock` on every call, competing with the device callback. During heavy combat (20–36 sounds/window), this adds measurable jitter.

**Proposed model:** The main thread writes gameplay events into a fixed-capacity ring buffer (matching the existing `GameplayEventQueue` layout). The audio thread drains it, applies spatial volume attenuation, and calls `SoundPool::Play`.

**Ring buffer design:** `GameplayEventQueue` is already a fixed-capacity 256-element value-type array — trivially wrappable into a SPSC (single-producer, single-consumer) ring buffer. The main thread is the sole producer (fixed-step update); the audio thread is the sole consumer.

**Signals from main thread:**
- SPSC `GameplayEventQueue` snapshot (write-index atomic)
- `Vec2f listenerPosition` (atomic snapshot written each fixed step)
- `GameMode mode` (for SFX enable/disable)

**Expected gain:** All `PlaySound`/`SetSoundVolume` lock contention moves off main thread. `audio.route_step` effectively disappears from `frame.cpu_work`. Secondary benefit: SFX latency becomes decoupled from frame-rate jitter.

**Complexity:** Moderate. Requires SPSC queue or double-buffered event copy + listener position atomic.

**Note:** Moving only this item (without item 1) gives smaller gains; the two are best combined.

---

### 3. Use `SetAudioStreamCallback` instead of `IsAudioStreamProcessed` + `UpdateAudioStream`

**What it is:** raylib exposes `SetAudioStreamCallback(stream, callback)`. The registered callback is invoked from `ReadAudioBufferFramesInInternalFormat` inside the device callback, while the global lock is already held. It receives `(void* framesOut, unsigned int frameCount)` and is responsible for writing samples directly.

**Benefit:** No polling (`IsAudioStreamProcessed`), no separate `UpdateAudioStream` call, no lock acquisition from user code — the callback runs on the audio device thread at exactly the right moment. This is the canonical low-latency streaming model in miniaudio.

**Constraints:**
- The callback runs **on the audio device thread** (inside the mixing callback). It must be non-blocking and fast — no mutex lock, no allocation, no file I/O.
- Synthesis (`FillBuffer`) must happen ahead of time, with output staged into a pre-filled ring buffer that the callback drains.
- This effectively requires a small SPSC ring buffer between a synthesis thread and the callback.

**Full architecture:**
```
Synthesis thread        SPSC ring buffer        Device callback (audio thread)
─────────────────       ───────────────         ──────────────────────────────
FillBuffer()     ──→    [ buf0 | buf1 | ... ]  ──→  callback drains samples
(runs freely)           (pre-filled ahead)           (lock already held; no overhead)
```

**Complexity:** High. Requires synthesis thread + ring buffer + callback registration. Worth considering if item 1 still shows residual upload stalls in future profiles.

---

### 4. Eliminate the unbounded catch-up loop

**Current behavior:** `MenuMusicPlayer::Update` runs `while (IsAudioStreamProcessed(stream_)) { FillBuffer(); UpdateAudioStream(...); }`. If the main thread was delayed (e.g. by a long fixed step), this loop may execute multiple times in one frame to fill any drained buffers, compounding the stall.

**Evidence:** Profile windows with `max(buf/call) > 1` and elevated `upload_total` confirmed multiple buffers filled per update during spike frames.

**Fix (independent of threading):** Cap the loop to one buffer per `Update` call. Audio will underrun in extreme cases (dropout), but the alternative is multi-frame stalls. With `kSampleBufferSamples = 1536` / 16 kHz, each buffer is 96 ms, so a cap of 1 means a potential 96 ms audio gap only when the game is already dropping frames severely. This is acceptable for a game target.

```cpp
// Change:
while (IsAudioStreamProcessed(stream_)) { ... }
// To:
if (IsAudioStreamProcessed(stream_)) { ... }
```

**Complexity:** Trivial. Can be done independently and immediately profiled.

---

### 5. Fix `XDG_RUNTIME_DIR` in the launch script (environment, not code)

**Status:** Resolved in profile-50 by fixing directory ownership on device. However, the fix was applied manually in a shell session and may not survive reboots or PortMaster re-launches.

**Action:** Ensure the launch wrapper script sets and creates the runtime dir before launching the binary:

```bash
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
mkdir -p "$XDG_RUNTIME_DIR/pulse"
chmod 700 "$XDG_RUNTIME_DIR" "$XDG_RUNTIME_DIR/pulse"
```

Without this, miniaudio/ALSA falls back to a degraded PulseAudio path that introduced multi-millisecond upload stalls (confirmed via profile-47/48 vs profile-50 comparison).

**Complexity:** Trivial (one-line script addition). High impact if the fix is not persistent.

---

## Buffer size tuning summary

| `kSampleBufferSamples` | Buffer duration (16 kHz) | Spike frequency (spikes/1k frames) | `fill_buffer` win max | `cpu_work` over 15 ms | Notes |
|------------------------|--------------------------|------------------------------------|-----------------------|-----------------------|-------|
| 1024 | 64 ms | 16.35 | ~1.6 ms | 1/53 windows | Frequent small spikes |
| 1536 | 96 ms | ~6–10 (estimated) | ~2.5 ms | TBD (profile-53) | **Current setting** |
| 2048 | 128 ms | 6.04 | ~3.2 ms | 3/91 windows | Good spike rate, heavier fill |
| 4096 | 256 ms | 5.34 | ~7.3 ms | 7/92 windows | Fill cost dominates; net worse |

Diminishing returns above 2048; 4096 worsens `cpu_work` overrun count despite fewer spike events.

---

## Priority order

| # | Action | Complexity | Expected gain |
|---|--------|------------|---------------|
| 5 | Fix `XDG_RUNTIME_DIR` in launch script permanently | Trivial | Removes environmental upload stalls |
| 4 | Cap music catch-up loop to 1 buffer per frame | Trivial | Eliminates multi-buffer stall compounding |
| 1 | Move `MenuMusicPlayer::Update` to audio thread | Moderate | Removes `gameplay.music` from `frame.cpu_work` entirely |
| 2 | Move `AudioEventRouter::RouteStep` to audio thread | Moderate | Removes SFX lock contention from `frame.cpu_work` |
| 3 | Switch to `SetAudioStreamCallback` + synthesis ring buffer | High | Lowest possible latency; eliminates upload-side stalls completely |

---

*Derived from handheld profiling runs 47–53 and raylib/miniaudio source review. Line numbers refer to the tree at authoring time and may drift.*
