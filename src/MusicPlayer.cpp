// ============================================================
//  MusicPlayer.cpp  —  VibePulse ENG5220
//  Auto-advances to next random track when current track ends
// ============================================================

#include "MusicPlayer.h"
#include <stdexcept>
#include <chrono>

MusicPlayer::MusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend)
        throw std::invalid_argument("MusicPlayer: backend must not be null");

    // Start background monitor thread
    m_monitor = std::thread(&MusicPlayer::monitorLoop, this);
}

MusicPlayer::~MusicPlayer() {
    // Stop monitor thread
    m_monitorStop.store(true);
    if (m_monitor.joinable())
        m_monitor.join();

    // Stop crossfade thread
    m_stopRequested.store(true);
    if (m_worker.joinable())
        m_worker.join();

    for (auto& handles : m_zoneHandles)
        for (int h : handles)
            m_backend->freeTrack(h);
}

// ─────────────────────────────────────────────────────────────
//  Load
// ─────────────────────────────────────────────────────────────
bool MusicPlayer::loadZone(MusicZone zone,
                            const std::vector<std::string>& paths) {
    auto& handles = m_zoneHandles[static_cast<int>(zone)];
    for (const auto& p : paths) {
        int h = m_backend->loadTrack(p);
        if (h < 0) return false;
        handles.push_back(h);
    }

    // Auto-start Zone 1 first track (play once, monitor will loop)
    if (zone == MusicZone::ZONE_1 && m_currentHandle < 0 && !handles.empty()) {
        m_currentHandle = handles[0];
        m_backend->play(m_currentHandle, 0);  // 0 = play once
        m_backend->setVolume(m_currentHandle, 128);
        m_volIn.store(128);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
//  BPM → zone
// ─────────────────────────────────────────────────────────────
void MusicPlayer::updateBPM(int bpm) {
    MusicZone target = bpmToZone(bpm);
    if (target != m_currentZone.load())
        crossfadeTo(target);
}

// ─────────────────────────────────────────────────────────────
//  Pick random (optionally excluding current track)
// ─────────────────────────────────────────────────────────────
int MusicPlayer::pickRandom(MusicZone zone) {
    return pickRandomExcept(zone, -1);
}

int MusicPlayer::pickRandomExcept(MusicZone zone, int excludeHandle) {
    auto& handles = m_zoneHandles[static_cast<int>(zone)];
    if (handles.empty()) return -1;

    // If only one track, just return it
    if (handles.size() == 1) return handles[0];

    // Pick randomly, avoiding the current track to prevent repeat
    int picked = excludeHandle;
    int attempts = 0;
    while (picked == excludeHandle && attempts < 10) {
        std::uniform_int_distribution<int> dist(
            0, static_cast<int>(handles.size()) - 1);
        picked = handles[dist(m_rng)];
        ++attempts;
    }
    return picked;
}

// ─────────────────────────────────────────────────────────────
//  Background monitor — detects when current track finishes
//  and auto-advances to next track in the same zone
// ─────────────────────────────────────────────────────────────
void MusicPlayer::monitorLoop() {
    while (!m_monitorStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Don't interfere during a crossfade
        if (m_crossfading.load()) continue;

        int handle = m_currentHandle;
        if (handle < 0) continue;

        // Check if current track has finished
        if (m_backend->isFinished(handle)) {
            // Pick a different random track from the same zone
            MusicZone zone = m_currentZone.load();
            int nextHandle = pickRandomExcept(zone, handle);
            if (nextHandle < 0) continue;

            // Simple switch: start new track at full volume
            // (no crossfade needed for same-zone auto-advance)
            m_backend->halt(handle);
            m_currentHandle = nextHandle;
            m_backend->play(nextHandle, 0);
            m_backend->setVolume(nextHandle, 128);
        }
    }
}

// ─────────────────────────────────────────────────────────────
//  Crossfade to a different zone
// ─────────────────────────────────────────────────────────────
void MusicPlayer::crossfadeTo(MusicZone next) {
    if (m_currentZone.load() == next) return;
    if (!m_backend->isReady())        return;

    // Pick a track from the new zone (avoid current if same zone somehow)
    int hIn = pickRandomExcept(next, m_currentHandle);
    if (hIn < 0) return;

    m_stopRequested.store(true);
    if (m_worker.joinable())
        m_worker.join();

    m_stopRequested.store(false);
    m_crossfading.store(true);

    int hOut = m_currentHandle;
    m_currentHandle = hIn;

    m_backend->play(hIn, 0);
    m_backend->setVolume(hIn, 0);

    m_worker = std::thread([this, hOut, hIn, next]() {
        runCrossfade(hOut, hIn, next);
    });
    m_worker.detach();
}

// ─────────────────────────────────────────────────────────────
//  Crossfade worker
// ─────────────────────────────────────────────────────────────
void MusicPlayer::runCrossfade(int hOut, int hIn, MusicZone next) {
    for (int step = 1; step <= CROSSFADE_STEPS; ++step) {
        if (m_stopRequested.load()) {
            m_crossfading.store(false);
            return;
        }
        int volIn  = static_cast<int>((128.0f * step) / CROSSFADE_STEPS);
        int volOut = 128 - volIn;

        if (hIn  >= 0) m_backend->setVolume(hIn,  volIn);
        if (hOut >= 0) m_backend->setVolume(hOut, volOut);

        m_volIn.store(volIn);
        m_volOut.store(volOut);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(CROSSFADE_STEP_MS));
    }

    if (hIn  >= 0) m_backend->setVolume(hIn,  128);
    if (hOut >= 0) {
        m_backend->setVolume(hOut, 0);
        m_backend->halt(hOut);
    }

    m_volIn.store(128);
    m_volOut.store(0);
    m_currentZone.store(next);
    m_crossfading.store(false);

    if (m_onTransition) m_onTransition(next);
}

// ─────────────────────────────────────────────────────────────
//  Callback setter
// ─────────────────────────────────────────────────────────────
void MusicPlayer::setTransitionCallback(std::function<void(MusicZone)> cb) {
    m_onTransition = std::move(cb);
}
