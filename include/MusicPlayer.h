#pragma once
// ============================================================
//  MusicPlayer.h  —  VibePulse ENG5220
//  Responsibility: Audio-only. Receives BPM, drives transitions.
//  SRP: No sensor logic, no UI, no BPM calculation here.
// ============================================================

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>

// ── Abstraction layer so unit-tests can stub the SDL2 backend ──
struct IAudioBackend {
    virtual ~IAudioBackend() = default;

    // Load a track; returns opaque handle id (≥0) or -1 on failure
    virtual int  loadTrack(const std::string& path)   = 0;
    // Free a loaded track by its handle id
    virtual void freeTrack(int id)                    = 0;
    // Play track (loop = -1 → infinite)
    virtual void play(int id, int loops = -1)         = 0;
    // Set per-channel volume [0..128]
    virtual void setVolume(int id, int vol)           = 0;
    // Halt playback for a specific handle
    virtual void halt(int id)                         = 0;
    // Returns true when audio subsystem is ready
    virtual bool isReady() const                      = 0;
};

// ── Heart-rate driven music mode ──────────────────────────────
enum class MusicMode { CALM, ACTIVE };

constexpr int BPM_CALM_THRESHOLD   = 80;   // ≤ 80 BPM  → Calm
constexpr int BPM_ACTIVE_THRESHOLD = 100;  // ≥ 100 BPM → Active
constexpr int CROSSFADE_STEPS      = 20;
constexpr int CROSSFADE_STEP_MS    = 50;   // 20 × 50 ms = 1 s total

// ─────────────────────────────────────────────────────────────
//  MusicPlayer  — public API
// ─────────────────────────────────────────────────────────────
class MusicPlayer {
public:
    // Inject audio backend (real or mock)
    explicit MusicPlayer(std::shared_ptr<IAudioBackend> backend);
    ~MusicPlayer();

    // Preload tracks for both modes (call once at startup)
    bool loadTracks(const std::string& calmPath,
                    const std::string& activePath);

    // Called by the heart-rate pipeline — drives mode transitions
    void updateBPM(int bpm);

    // Non-blocking 1-second crossfade; safe to call from any thread
    // Spawns a worker thread; previous worker is gracefully interrupted.
    void crossfade(MusicMode nextMode);

    // Query
    MusicMode   currentMode()   const { return m_currentMode.load(); }
    bool        isCrossfading() const { return m_crossfading.load(); }

    // Callback fired when a transition completes (optional)
    void setTransitionCallback(std::function<void(MusicMode)> cb);

    // ── Testability helpers ───────────────────────────────────
    // Returns the raw volume currently applied to each handle [0..128]
    int debugVolumeCalm()   const { return m_volCalm.load();   }
    int debugVolumeActive() const { return m_volActive.load(); }

private:
    void runCrossfade(MusicMode from, MusicMode to);

    std::shared_ptr<IAudioBackend>      m_backend;
    int                                 m_handleCalm   = -1;
    int                                 m_handleActive = -1;

    std::atomic<MusicMode>              m_currentMode  { MusicMode::CALM };
    std::atomic<bool>                   m_crossfading  { false };
    std::atomic<bool>                   m_stopRequested{ false };
    std::atomic<int>                    m_volCalm      { 128 };
    std::atomic<int>                    m_volActive    { 0   };

    std::thread                         m_worker;
    std::function<void(MusicMode)>      m_onTransition;
};
