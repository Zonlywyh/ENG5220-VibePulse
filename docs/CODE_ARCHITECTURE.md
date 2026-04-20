# Code Architecture (ENG5220-VibePulse)

This document explains the current code structure (modules, classes, key
functions, and data flow). It is written from the perspective of "how the code
works today", not only the PRD/ideal design.

## Repository layout (key files)

- `src/main.cpp`
  - Current realtime heart-rate monitor entrypoint (sensor + BPM calc + console output).
  - It connects the sensor, BPM calculation, console output, and audio playback.
- `include/sensor.h` + `src/Sensor.cpp`
  - MAX30102 hardware driver (I2C + GPIO interrupt via libgpiod).
  - Exposes an event/callback API that pushes batches of `Sample`.
- `include/HeartRateCalculator.h` + `src/HeartRateCalculator.cpp`
  - Lightweight BPM calculation from the MAX30102 IR channel.
  - Thread-safe getters for `fingerDetected()` and `getLatestBpm()`.
- `include/ZoneMusicPlayer.h + src/ZoneMusicPlayer.cpp`
  - Zone-based music player driven by BPM ranges, with crossfades and track switching..
  - Audio backend is injected through IAudioBackend for testability.
- `include/SDL2AudioBackend.h`
  - Real audio backend implemented with SDL2 + SDL2_mixer (loads WAV, sets per-channel volume).
- `include/I2C.h` + `tests/main.cpp`
  - Simple I2C register read/write helper and a small MAX30102 communication smoke test.
- `include/dsp.h` + `src/dsp.cpp`
  - An experimental/alternative DSP implementation (not currently wired into `src/main.cpp`).
- `include/VibePulse.h`
  - A "concept" header describing module responsibilities; not used by the build.

## Runtime pipeline (intended integration)

The intended end-to-end pipeline is:

1. `Max30102Sensor` acquires data from the MAX30102 (event-driven via GPIO DRDY).
2. Sensor pushes `std::vector<Sample>` to a callback (batch processing).
3. `HeartRateCalculator` consumes `Sample.ir` and produces a stable BPM estimate.
4. The app forwards BPM to AudioService.
5. AudioService forwards BPM to ZoneMusicPlayer::updateBPM(...).
6. ZoneMusicPlayer maps BPM to zone1 ~ zone6 and performs crossfades when switching zones.
7. SDL2AudioBackend performs the actual playback and volume changes.

Today, src/main.cpp integrates the full sensor, BPM, console, and audio path.

## I/O map (what each module talks to)

- MAX30102 sensor I/O (`include/sensor.h`, `src/Sensor.cpp`)
  - **I2C input/output:** opens `/dev/i2c-<bus>` (default bus `1`) and talks to slave address `0x57`
    using `open()` + `ioctl(I2C_SLAVE)` + `read()`/`write()` in `Max30102Sensor::initialize()`,
    `Max30102Sensor::readRegister()`, `Max30102Sensor::writeRegister()`, `Max30102Sensor::readFifo()`.
  - **GPIO interrupt input:** uses libgpiod on `/dev/gpiochip0` and requests a falling-edge event line
    for `interrupt_pin_` in `Max30102Sensor::initialize()`, then blocks on
    `gpiod_line_request_wait_edge_events()` in `Max30102Sensor::dataWorker()`.
- BPM calculator I/O (`include/HeartRateCalculator.h`, `src/HeartRateCalculator.cpp`)
  - **No direct hardware I/O.** Input is `Sample.ir` values from the sensor callback.
  - Output is an in-memory state (`finger_detected_`, `latest_bpm_`) queried via getters.
- Music player I/O (`include/MusicPlayer.h`, `src/MusicPlayer.cpp`)
  - **Audio file input:** track paths passed to `MusicPlayer::loadTracks(calmPath, activePath)`.
  - **Audio device output:** delegated to `IAudioBackend` (real implementation uses SDL2/SDL2_mixer).
- SDL2 backend I/O (`include/SDL2AudioBackend.h`)
  - **Audio device output:** `SDL_Init(SDL_INIT_AUDIO)` + `Mix_OpenAudio()` opens the system audio device.
  - **Audio file input:** `Mix_LoadWAV(path)` loads WAV files into `Mix_Chunk`.
- Console/logging I/O (current state)
  - `src/main.cpp`, `src/Sensor.cpp`, `src/HeartRateCalculator.cpp` contain `std::cout/std::cerr` debug prints.

## Callbacks (signatures, who calls, thread context)

### 1) Sensor data callback: `Max30102Sensor::DataCallback`

Signature (declared in `include/sensor.h`):

- `using DataCallback = std::function<void(const std::vector<Sample>& samples)>;`

Registration (called by the application, e.g. `src/main.cpp`):

- `Max30102Sensor::setDataCallback(DataCallback cb)`

Invocation (called by the sensor driver, in `src/Sensor.cpp`):

- `Max30102Sensor::readFifo()` reads FIFO samples and then calls `cb(new_samples)`.

Thread context:

- The callback runs on the sensor's background thread (`reader_thread_`) created in `Max30102Sensor::start()`.
- Important detail: `readFifo()` copies `data_callback_` under `mutex_` and invokes it after releasing the lock,
  so the callback must be fast and must not call back into `Max30102Sensor` in a way that assumes it still holds the lock.

Data ownership:

- `samples` points to a `std::vector<Sample>` owned by the caller stack frame; it is only valid during the callback.
  If the application needs to store data, it should copy it.

### 2) Music transition callback: `ZoneMusicPlayer::setTransitionCallback`

Signature (declared in `include/MusicPlayer.h`):

- `void setTransitionCallback(std::function<void(int zone)> cb);`

Invocation (in `src/MusicPlayer.cpp`):

- `MusicPlayer::runCrossfade()` calls `m_onTransition(to)` after the crossfade completes.

Thread context:

- The callback runs on the music worker thread started by `MusicPlayer::crossfade()`.
  If it needs to touch UI/console/global state, it should be thread-safe.

## Concurrency model (where threads run)

- Sensor thread:
  - `Max30102Sensor::start()` spawns `reader_thread_` running `dataWorker()`.
  - `dataWorker()` blocks on `gpiod_line_request_wait_edge_events()` and calls `readFifo()` on edges.
- Main thread:
  - `src/main.cpp` registers callbacks and updates the console from a separate presenter thread.
- Audio threads:
  - ZoneMusicPlayer uses an internal event thread plus a worker thread for crossfades.

Thread-safety notes:

- `Max30102Sensor` protects its callback pointer and sample buffer with `mutex_`.
- `HeartRateCalculator` protects its internal filter/peak state with `mutex_`.
- `MusicPlayer` uses atomics for mode/volumes and a joinable worker thread for crossfade control.

## Module API overview (classes and functions)

### MAX30102 driver (`include/sensor.h`, `src/Sensor.cpp`)

Core data types:

- `struct Sample { float red; float ir; }` (normalized roughly to `[0,1]`)
- Enums: `SampleAverage`, `SampleRate`, `LedPulseWidth`

Key class: `class Max30102Sensor`

- `Max30102Sensor(int interruptPin, SampleAverage avg, SampleRate rate, LedPulseWidth width)`
  - Stores config parameters and prepares for initialization.
- `bool initialize()`
  - Opens `/dev/i2c-*`, sets I2C slave address, checks part ID, configures GPIO interrupt, and programs sensor registers.
- `void start()` / `void stop()`
  - Starts/stops the background acquisition thread.
- `void setDataCallback(DataCallback cb)`
  - Registers a callback invoked with `std::vector<Sample>` when new FIFO samples are read.
- `std::vector<Sample> getLatestSamples(size_t maxCount) const`
  - Returns recent samples from an internal sliding window.

Internal implementation points:

- `dataWorker()`: waits for DRDY edges (falling edge) and triggers `readFifo()`.
- `readFifo()`: reads FIFO pointers, computes `samples_to_read`, reads `REG_FIFO_DATA` (6 bytes per sample),
  converts 18-bit raw values, updates buffers, and calls the callback outside the lock.
- `writeRegister()` / `readRegister()`: low-level I2C register helpers.

### BPM calculation (`include/HeartRateCalculator.h`, `src/HeartRateCalculator.cpp`)

Key class: `class HeartRateCalculator`

- `explicit HeartRateCalculator(double sampleRateHz)`
  - Sets filter constants and thresholds.
- `void processIrSamples(const std::vector<float>& irSamples)`
  - Iterates over new samples and calls `processOne(s.ir)` under a mutex.
- `std::optional<double> getLatestBpm() const`
  - Returns the latest averaged BPM (or empty if not enough data).
- `bool fingerDetected() const`
  - Returns whether IR amplitude passes a threshold.

Internal logic:

- `processOne(float ir)`: DC tracking + smoothing + simple 3-point peak detection.
- Uses `bpm_window_` (size 5) to average recent BPM values for stability.

### Music state machine (`include/MusicPlayer.h`, `src/MusicPlayer.cpp`)

Data types:

- `enum class MusicMode { CALM, ACTIVE };`
- Thresholds:
  - `BPM_CALM_THRESHOLD = 80`
  - `BPM_ACTIVE_THRESHOLD = 100`
  - Hysteresis band: 81..99 keeps current mode.

Abstraction:

- `struct IAudioBackend`
  - `loadTrack(path)`, `freeTrack(id)`, `play(id, loops)`, `setVolume(id, vol)`, `halt(id)`, `isReady()`

Key class: `class MusicPlayer`

- `explicit MusicPlayer(std::shared_ptr<IAudioBackend> backend)`
  - Backend is dependency-injected to keep tests hardware-free.
- `bool loadTracks(calmPath, activePath)`
  - Loads and starts both tracks (one at full volume, the other muted).
- `void updateBPM(int bpm)`
  - Converts BPM to target mode and triggers transition if needed.
- `void crossfade(MusicMode nextMode)`
  - Non-blocking 1s crossfade in a background thread; interrupts previous crossfade if any.
- `void setTransitionCallback(std::function<void(MusicMode)> cb)`
  - Optional callback on transition completion.
- Debug helpers:
  - `debugVolumeCalm()`, `debugVolumeActive()`

### SDL2 audio backend (`include/SDL2AudioBackend.h`)

Key class: `class SDL2AudioBackend : public IAudioBackend`

- Initializes SDL audio + `Mix_OpenAudio()`.
- Loads WAV via `Mix_LoadWAV()`.
- Uses one mixer channel per track and controls volume with `Mix_Volume()`.

### I2C helper (`include/I2C.h`, `tests/main.cpp`)

Key class: `class I2CDevice`

- `I2CDevice(bus, addr)`, `writeRegister(reg, value)`, `readRegister(reg)`, `readRegisters(reg, buf, len)`

`tests/main.cpp` uses it to:

- Read `REG_PART_ID` to validate the MAX30102 connection.
- Write basic configuration registers as a smoke test.

### Experimental DSP (`include/dsp.h`, `src/dsp.cpp`)

`HeartRateDSP` provides:

- `unpackRawData()` (3 bytes -> 18-bit)
- `processSample()` (filtering)
- `detectHeartRate()` (peak detection -> BPM)

It is currently not integrated into the main pipeline.

## Entry points and how to run

This project uses CMake for builds and tests.

## build
```bash
cmake -S . -B build
cmake --build build
```


## Known integration notes (important when building on Linux)

- `include/sensor.h` is a lowercase filename, but some sources include `Sensor.h`.
  On Linux (case-sensitive) that will fail. Keep include casing consistent.
- `include/VibePulse.h` defines another `MusicPlayer` class (concept-only). Avoid mixing it with the real player in `include/MusicPlayer.h`.
- `assets/music/` currently does not contain real WAV files; `SDL2AudioBackend` expects WAV (because it uses `Mix_LoadWAV`).
