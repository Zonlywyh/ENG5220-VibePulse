#pragma once
// ============================================================
//  ZoneMusicPlayer.h — VibePulse ENG5220
//  6-zone BPM-driven music player: multiple tracks per zone,
//  one track playing at a time, auto-advance + crossfade.
// ============================================================

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <random>

// ── Audio backend abstraction ─────────────────────────────────
struct IAudioBackend {
    virtual ~IAudioBackend() = default;
    virtual int  loadTrack(const std::string& path) = 0;
    virtual void freeTrack(int id)                  = 0;
    virtual void play(int id, int loops = 0)        = 0;  // loops=0 → play once
    virtual void setVolume(int id, int vol)         = 0;
    virtual void halt(int id)                       = 0;
    virtual bool isReady() const                    = 0;
    virtual bool isFinished(int id) const           = 0;
};

constexpr int ZONE_COUNT        = 6;
constexpr int CROSSFADE_STEPS   = 20;
constexpr int CROSSFADE_STEP_MS = 50;   // 20 × 50ms = 1s total

// Maps BPM to zone 1..6
inline int bpmToZone(int bpm) {
    if (bpm <  80) return 1;
    if (bpm < 100) return 2;
    if (bpm < 120) return 3;
    if (bpm < 140) return 4;
    if (bpm < 160) return 5;
    return 6;
}

// ─────────────────────────────────────────────────────────────
//  ZoneMusicPlayer — public API
// ─────────────────────────────────────────────────────────────
class ZoneMusicPlayer {
public:
    explicit ZoneMusicPlayer(std::shared_ptr<IAudioBackend> backend);
    ~ZoneMusicPlayer();

    // Load tracks for a zone (1..6). Call once per zone; multiple tracks supported.
    bool loadZone(int zone, const std::vector<std::string>& paths);

    // Feed BPM updates from the heart-rate pipeline.
    void updateBPM(int bpm);

    // Force a zone switch with crossfade (1..6). Safe to call from any thread.
    void setZone(int zone);

    int  currentZone()    const { return m_currentZone.load(); }
    bool isCrossfading()  const { return m_crossfading.load(); }
    int  debugVolumeIn()  const { return m_volIn.load();  }
    int  debugVolumeOut() const { return m_volOut.load(); }

    void setTransitionCallback(std::function<void(int zone)> cb);

private:
    int  pickRandomExcept(int zone, int excludeHandle);
    void runCrossfade(int hOut, int hIn, int next);
    void monitorLoop();
    void stopWorker();

private:
    std::shared_ptr<IAudioBackend>  m_backend;

    std::vector<int>                m_zoneHandles[ZONE_COUNT];
    int                             m_currentHandle{ -1 };

    std::atomic<int>                m_currentZone  { 1 };
    std::atomic<bool>               m_crossfading  { false };
    std::atomic<bool>               m_stopRequested{ false };
    std::atomic<int>                m_volIn        { 128 };
    std::atomic<int>                m_volOut       {   0 };

    std::thread                     m_worker;
    std::thread                     m_monitor;
    std::atomic<bool>               m_monitorStop  { false };

    // Lets runCrossfade wake immediately when interrupted instead of sleeping.
    std::mutex                      m_cv_mutex;
    std::condition_variable         m_cv;

    std::function<void(int)>        m_onTransition;
    std::mt19937                    m_rng{ std::random_device{}() };
};
