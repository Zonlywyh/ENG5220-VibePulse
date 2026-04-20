#pragma once
// ============================================================
//  ZoneMusicPlayer.h — VibePulse ENG5220
//  6-zone BPM-driven music player: multiple tracks per zone,
//  one track playing at a time, auto-advance + crossfade.
// ============================================================

#include "MusicPlayer.h"  // for IAudioBackend, CROSSFADE_STEPS, CROSSFADE_STEP_MS

#include <string>
#include <vector>
#include <array>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <random>
#include <optional>
#include <unordered_map>

constexpr int ZONE_COUNT = 6;

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
    static constexpr int kZoneCount = ZONE_COUNT;

    explicit ZoneMusicPlayer(std::shared_ptr<IAudioBackend> backend);
    ~ZoneMusicPlayer();

    // Load tracks for a zone (1..6). Supports multiple tracks per zone.
    bool loadZone(int zone, const std::vector<std::string>& paths);

    // Convenience: load one track per zone (zone1..zone6).
    bool loadZoneTracks(const std::array<std::string, kZoneCount>& paths);

    // Feed BPM updates from the heart-rate pipeline.
    void updateBPM(int bpm);

    // Force a zone switch with crossfade (1..6). Safe to call from any thread.
    void setZone(int zone);

    int  currentZone()   const { return m_currentZone.load(); }
    int  targetZone()    const { return m_currentZone.load(); }  // same: no separate target in this model
    bool isCrossfading() const { return m_crossfading.load(); }
    int  debugVolumeIn() const { return m_volIn.load();  }
    int  debugVolumeOut()const { return m_volOut.load(); }

    // Path of the currently playing track (for status display).
    std::optional<std::string> currentTrackPath() const;
    std::optional<std::string> targetTrackPath()  const;

    void setTransitionCallback(std::function<void(int zone)> cb);

private:
    int  pickRandomExcept(int zone, int excludeHandle);
    void runCrossfade(int hOut, int hIn, int next);
    void monitorLoop();
    void stopWorker();

    std::shared_ptr<IAudioBackend>        m_backend;

    std::vector<int>                      m_zoneHandles[ZONE_COUNT];
    std::unordered_map<int, std::string>  m_handlePaths;  // handle → file path
    int                                   m_currentHandle{ -1 };

    std::atomic<int>                      m_currentZone  { 1 };
    std::atomic<bool>                     m_crossfading  { false };
    std::atomic<bool>                     m_stopRequested{ false };
    std::atomic<int>                      m_volIn        { 128 };
    std::atomic<int>                      m_volOut       {   0 };

    std::thread                           m_worker;
    std::thread                           m_monitor;
    std::atomic<bool>                     m_monitorStop  { false };

    mutable std::mutex                    m_pathMutex;
    std::string                           m_currentTrackPath;

    std::mutex                            m_cv_mutex;
    std::condition_variable               m_cv;

    std::function<void(int)>              m_onTransition;
    std::mt19937                          m_rng{ std::random_device{}() };
};
