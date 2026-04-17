// ============================================================
//  MusicPlayer.cpp  —  VibePulse ENG5220
//  6-zone BPM-driven music player with 1-second crossfade
// ============================================================

#include "MusicPlayer.h"
#include <stdexcept>
#include <chrono>

// ─────────────────────────────────────────────────────────────
//  Ctor / Dtor
// ─────────────────────────────────────────────────────────────
MusicPlayer::MusicPlayer(std::shared_ptr<IAudioBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend)
        throw std::invalid_argument("MusicPlayer: backend must not be null");
}

MusicPlayer::~MusicPlayer() {
    m_stopRequested.store(true);
    if (m_worker.joinable())
        m_worker.join();

    for (auto& handles : m_zoneHandles)
        for (int h : handles)
            m_backend->freeTrack(h);
}

// ─────────────────────────────────────────────────────────────
//  Load tracks for a zone
// ─────────────────────────────────────────────────────────────
bool MusicPlayer::loadZone(MusicZone zone,
                            const std::vector<std::string>& paths) {
    auto& handles = m_zoneHandles[static_cast<int>(zone)];

    for (const auto& p : paths) {
        int h = m_backend->loadTrack(p);
        if (h < 0) return false;
        handles.push_back(h);
    }

    // Auto-start Zone 1 on first load
    if (zone == MusicZone::ZONE_1 && m_currentHandle < 0 && !handles.empty()) {
        m_currentHandle = handles[0];
        m_backend->play(m_currentHandle, -1);
        m_backend->setVolume(m_currentHandle, 128);
        m_volIn.store(128);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
//  BPM → zone transition
// ─────────────────────────────────────────────────────────────
void MusicPlayer::updateBPM(int bpm) {
    MusicZone target = bpmToZone(bpm);
    if (target != m_currentZone.load())
        crossfadeTo(target);
}

// ─────────────────────────────────────────────────────────────
//  Pick a random track handle from a zone
// ─────────────────────────────────────────────────────────────
int MusicPlayer::pickRandom(MusicZone zone) {
    auto& handles = m_zoneHandles[static_cast<int>(zone)];
    if (handles.empty()) return -1;
    std::uniform_int_distribution<int> dist(0, static_cast<int>(handles.size()) - 1);
    return handles[dist(m_rng)];
}

// ─────────────────────────────────────────────────────────────
//  Non-blocking crossfade launcher
// ─────────────────────────────────────────────────────────────
void MusicPlayer::crossfadeTo(MusicZone next) {
    if (m_currentZone.load() == next) return;
    if (!m_backend->isReady())        return;

    int hIn = pickRandom(next);
    if (hIn < 0) return;

    // Interrupt any in-flight crossfade
    m_stopRequested.store(true);
    if (m_worker.joinable())
        m_worker.join();

    m_stopRequested.store(false);
    m_crossfading.store(true);

    int hOut = m_currentHandle;
    m_currentHandle = hIn;

    m_backend->play(hIn, -1);
    m_backend->setVolume(hIn, 0);

    m_worker = std::thread([this, hOut, hIn, next]() {
        runCrossfade(hOut, hIn, next);
    });
    m_worker.detach();
}

// ─────────────────────────────────────────────────────────────
//  Crossfade worker — fades hOut 128→0, hIn 0→128 over 1s
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
