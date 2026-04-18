#pragma once
// ============================================================
//  MusicPlayer.h  —  VibePulse ENG5220
//  6-zone BPM-driven music player with crossfade
//  Auto-advances to next random track within the same zone
//  when the current track finishes.
// ============================================================

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
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
    // Returns true if the track has finished playing
    virtual bool isFinished(int id) const           = 0;
};

// ── 6 BPM zones ───────────────────────────────────────────────
enum class MusicZone {
    ZONE_1 = 0,   // 60–79   BPM
    ZONE_2,       // 80–99   BPM
    ZONE_3,       // 100–119 BPM
    ZONE_4,       // 120–139 BPM
    ZONE_5,       // 140–159 BPM
    ZONE_6        // 160–180 BPM
};

constexpr int ZONE_COUNT        = 6;
constexpr int CROSSFADE_STEPS   = 20;
constexpr int CROSSFADE_STEP_MS = 50;   // 20 × 50ms = 1s total

inline MusicZone bpmToZone(int bpm) {
    if (bpm <  80) return MusicZone::ZONE_1;
    if (bpm < 100) return MusicZone::ZONE_2;
    if (bpm < 120) return MusicZone::ZONE_3;
    if (bpm < 140) return MusicZone::ZONE_4;
    if (bpm < 160) return MusicZone::ZONE_5;
    return             MusicZone::ZONE_6;
}

// ─────────────────────────────────────────────────────────────
//  MusicPlayer — public API
// ─────────────────────────────────────────────────────────────
class MusicPlayer {
public:
    explicit MusicPlayer(std::shared_ptr<IAudioBackend> backend);
    ~MusicPlayer();

    bool loadZone(MusicZone zone, const std::vector<std::string>& paths);
    void updateBPM(int bpm);
    void crossfadeTo(MusicZone next);

    MusicZone currentZone()   const { return m_currentZone.load(); }
    bool      isCrossfading() const { return m_crossfading.load(); }
    int debugVolumeIn()  const { return m_volIn.load();  }
    int debugVolumeOut() const { return m_volOut.load(); }

    void setTransitionCallback(std::function<void(MusicZone)> cb);

private:
    int  pickRandom(MusicZone zone);
    int  pickRandomExcept(MusicZone zone, int excludeHandle);
    void runCrossfade(int hOut, int hIn, MusicZone next);
    void monitorLoop();   // background thread: detects track end

    std::shared_ptr<IAudioBackend>  m_backend;

    std::vector<int>                m_zoneHandles[ZONE_COUNT];
    int                             m_currentHandle{ -1 };

    std::atomic<MusicZone>          m_currentZone  { MusicZone::ZONE_1 };
    std::atomic<bool>               m_crossfading  { false };
    std::atomic<bool>               m_stopRequested{ false };
    std::atomic<int>                m_volIn        { 128 };
    std::atomic<int>                m_volOut       {   0 };

    std::thread                     m_worker;      // crossfade thread
    std::thread                     m_monitor;     // track-end monitor thread
    std::atomic<bool>               m_monitorStop  { false };

    std::function<void(MusicZone)>  m_onTransition;
    std::mt19937                    m_rng{ std::random_device{}() };
};
