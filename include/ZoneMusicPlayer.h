#pragma once
// ============================================================
//  ZoneMusicPlayer.h — VibePulse ENG5220
//  Responsibility: Multi-zone playback (zone1..zone6) + crossfade.
//  Notes:
//  - Uses IAudioBackend so it can run on SDL2 or a mock backend.
//  - Does NOT depend on sensors; main() feeds BPM values.
// ============================================================

#include "MusicPlayer.h"  // for IAudioBackend

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class ZoneMusicPlayer {
public:
    static constexpr int kZoneCount = 6;

    explicit ZoneMusicPlayer(std::shared_ptr<IAudioBackend> backend);
    ~ZoneMusicPlayer();

    // Loads and loops all zone tracks. Index 0 == zone1 ... index 5 == zone6.
    bool loadZoneTracks(const std::array<std::string, kZoneCount>& zonePaths);

    // Feed BPM updates from the heart-rate pipeline.
    // Mapping is project-defined; adjust thresholds here if needed.
    void updateBPM(int bpm);

    // Force a zone (1..6). Safe to call from any thread.
    void setZone(int zone);

    int  currentZone() const { return m_currentZone.load(); }
    bool isCrossfading() const { return m_crossfading.load(); }

    void setTransitionCallback(std::function<void(int zone)> cb);

private:
    // 1-second crossfade worker (CROSSFADE_* constants from MusicPlayer.h)
    void runCrossfade(int fromZone, int toZone);
    int  bpmToZone(int bpm) const;

    void stopWorker();
    void freeTracks();

private:
    std::shared_ptr<IAudioBackend> m_backend;
    std::array<int, kZoneCount>    m_handles{};

    std::atomic<int>               m_currentZone{1};  // 1..6
    std::atomic<bool>              m_crossfading{false};
    std::atomic<bool>              m_stopRequested{false};

    std::thread                    m_worker;
    std::function<void(int)>       m_onTransition;
};

